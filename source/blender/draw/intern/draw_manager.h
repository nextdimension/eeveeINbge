/*
 * Copyright 2016, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Blender Institute
 *
 */

/** \file draw_manager.h
 *  \ingroup draw
 */

/* Private functions / structs of the draw manager */

#ifndef __DRAW_MANAGER_H__
#define __DRAW_MANAGER_H__

#include "DRW_engine.h"
#include "DRW_render.h"

#include "BLI_linklist.h"
#include "BLI_threads.h"

#include "GPU_batch.h"
#include "GPU_framebuffer.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"

#include "draw_instance_data.h"



#include "../draw/engines/eevee/eevee_private.h" // ge transition (was a dirt compil fix for DRWMatrixState transfered in eevee_private.h)



/* Use draw manager to call GPU_select, see: DRW_draw_select_loop */
#define USE_GPU_SELECT

/* ------------ Profiling --------------- */

#define USE_PROFILE

#ifdef USE_PROFILE
#  include "PIL_time.h"

#  define PROFILE_TIMER_FALLOFF 0.04

#  define PROFILE_START(time_start) \
	double time_start = PIL_check_seconds_timer();

#  define PROFILE_END_ACCUM(time_accum, time_start) { \
	time_accum += (PIL_check_seconds_timer() - time_start) * 1e3; \
} ((void)0)

/* exp average */
#  define PROFILE_END_UPDATE(time_update, time_start) { \
	double _time_delta = (PIL_check_seconds_timer() - time_start) * 1e3; \
	time_update = (time_update * (1.0 - PROFILE_TIMER_FALLOFF)) + \
	              (_time_delta * PROFILE_TIMER_FALLOFF); \
} ((void)0)

#else  /* USE_PROFILE */

#  define PROFILE_START(time_start) ((void)0)
#  define PROFILE_END_ACCUM(time_accum, time_start) ((void)0)
#  define PROFILE_END_UPDATE(time_update, time_start) ((void)0)

#endif  /* USE_PROFILE */

/* ------------ Data Structure --------------- */
/**
 * Data structure containing all drawcalls organized by passes and materials.
 * DRWPass > DRWShadingGroup > DRWCall > DRWCallState
 *                           > DRWUniform
 **/

/* Used by DRWCallState.flag */
enum {
	DRW_CALL_CULLED                 = (1 << 0),
	DRW_CALL_NEGSCALE               = (1 << 1),
};

/* Used by DRWCallState.matflag */
enum {
	DRW_CALL_MODELINVERSE           = (1 << 0),
	DRW_CALL_MODELVIEW              = (1 << 1),
	DRW_CALL_MODELVIEWINVERSE       = (1 << 2),
	DRW_CALL_MODELVIEWPROJECTION    = (1 << 3),
	DRW_CALL_NORMALVIEW             = (1 << 4),
	DRW_CALL_NORMALWORLD            = (1 << 5),
	DRW_CALL_ORCOTEXFAC             = (1 << 6),
	DRW_CALL_EYEVEC                 = (1 << 7),
};

typedef struct DRWCallState {
	unsigned char flag;
	unsigned char cache_id;   /* Compared with DST.state_cache_id to see if matrices are still valid. */
	uint16_t matflag;         /* Which matrices to compute. */
	/* Culling: Using Bounding Sphere for now for faster culling.
	 * Not ideal for planes. */
	BoundSphere bsphere;
	/* Matrices */
	float model[4][4];
	float modelinverse[4][4];
	float modelview[4][4];
	float modelviewinverse[4][4];
	float modelviewprojection[4][4];
	float normalview[3][3];
	float normalworld[3][3]; /* Not view dependant */
	float orcotexfac[2][3]; /* Not view dependant */
	float eyevec[3];
} DRWCallState;

typedef enum {
	DRW_CALL_SINGLE,                 /* A single batch */
	DRW_CALL_INSTANCES,              /* Draw instances without any instancing attribs. */
	DRW_CALL_GENERATE,               /* Uses a callback to draw with any number of batches. */
} DRWCallType;

typedef struct DRWCall {
	struct DRWCall *next;
	DRWCallState *state;

	union {
		struct { /* type == DRW_CALL_SINGLE */
			Gwn_Batch *geometry;
		} single;
		struct { /* type == DRW_CALL_INSTANCES */
			Gwn_Batch *geometry;
			/* Count can be adjusted between redraw. If needed, we can add fixed count. */
			unsigned int *count;
		} instances;
		struct { /* type == DRW_CALL_GENERATE */
			DRWCallGenerateFn *geometry_fn;
			void *user_data;
		} generate;
	};

	DRWCallType type;
#ifdef USE_GPU_SELECT
	int select_id;
#endif
} DRWCall;

/* Used by DRWUniform.type */
typedef enum {
	DRW_UNIFORM_BOOL,
	DRW_UNIFORM_SHORT_TO_INT,
	DRW_UNIFORM_SHORT_TO_FLOAT,
	DRW_UNIFORM_INT,
	DRW_UNIFORM_FLOAT,
	DRW_UNIFORM_TEXTURE,
	DRW_UNIFORM_TEXTURE_PERSIST,
	DRW_UNIFORM_TEXTURE_REF,
	DRW_UNIFORM_BLOCK,
	DRW_UNIFORM_BLOCK_PERSIST
} DRWUniformType;

struct DRWUniform {
	DRWUniform *next; /* single-linked list */
	const void *value;
	int location;
	char type; /* DRWUniformType */
	char length; /* cannot be more than 16 */
	char arraysize; /* cannot be more than 16 too */
};

typedef enum {
	DRW_SHG_NORMAL,
	DRW_SHG_POINT_BATCH,
	DRW_SHG_LINE_BATCH,
	DRW_SHG_TRIANGLE_BATCH,
	DRW_SHG_INSTANCE,
	DRW_SHG_INSTANCE_EXTERNAL,
} DRWShadingGroupType;

struct DRWShadingGroup {
	DRWShadingGroup *next;

	GPUShader *shader;        /* Shader to bind */
	DRWUniform *uniforms;          /* Uniforms pointers */

	/* Watch this! Can be nasty for debugging. */
	union {
		struct { /* DRW_SHG_NORMAL */
			DRWCall *first, *last; /* Linked list of DRWCall or DRWCallDynamic depending of type */
		} calls;
		struct { /* DRW_SHG_***_BATCH */
			struct Gwn_Batch *batch_geom;     /* Result of call batching */
			struct Gwn_VertBuf *batch_vbo;
			unsigned int primitive_count;
		};
		struct { /* DRW_SHG_INSTANCE[_EXTERNAL] */
			struct Gwn_Batch *instance_geom;
			struct Gwn_VertBuf *instance_vbo;
			unsigned int instance_count;
			float instance_orcofac[2][3]; /* TODO find a better place. */
		};
	};

	DRWState state_extra;            /* State changes for this batch only (or'd with the pass's state) */
	DRWState state_extra_disable;    /* State changes for this batch only (and'd with the pass's state) */
	unsigned int stencil_mask;       /* Stencil mask to use for stencil test / write operations */
	DRWShadingGroupType type;

	/* Builtin matrices locations */
	int model;
	int modelinverse;
	int modelview;
	int modelviewinverse;
	int modelviewprojection;
	int normalview;
	int normalworld;
	int orcotexfac;
	int eye;
	uint16_t matflag; /* Matrices needed, same as DRWCall.flag */

#ifndef NDEBUG
	char attribs_count;
#endif

#ifdef USE_GPU_SELECT
	DRWInstanceData *inst_selectid;
	DRWPass *pass_parent; /* backlink to pass we're in */
	int override_selectid; /* Override for single object instances. */
#endif
};

#define MAX_PASS_NAME 32

struct DRWPass {
	/* Linked list */
	struct {
		DRWShadingGroup *first;
		DRWShadingGroup *last;
	} shgroups;

	DRWState state;
	char name[MAX_PASS_NAME];
};

typedef struct ViewUboStorage {
	DRWMatrixState matstate;
	float viewcamtexcofac[4];
	float clipplanes[2][4];
} ViewUboStorage;

/* ------------- DRAW MANAGER ------------ */

#define MAX_CLIP_PLANES 6 /* GL_MAX_CLIP_PLANES is at least 6 */

typedef struct DRWManager {
	/* TODO clean up this struct a bit */
	/* Cache generation */
	ViewportMemoryPool *vmempool;
	DRWInstanceDataList *idatalist;
	DRWInstanceData *common_instance_data[MAX_INSTANCE_DATA_SIZE];
	/* State of the object being evaluated if already allocated. */
	DRWCallState *ob_state;
	unsigned char state_cache_id; /* Could be larger but 254 view changes is already a lot! */

	/* Rendering state */
	GPUShader *shader;

	/* Managed by `DRW_state_set`, `DRW_state_reset` */
	DRWState state;
	DRWState state_lock;
	unsigned int stencil_mask;

	/* Per viewport */
	GPUViewport *viewport;
	struct GPUFrameBuffer *default_framebuffer;
	float size[2];
	float inv_size[2];
	float screenvecs[2][3];
	float pixsize;

	GLenum backface, frontface;

	struct {
		unsigned int is_select : 1;
		unsigned int is_depth : 1;
		unsigned int is_image_render : 1;
		unsigned int is_scene_render : 1;
		unsigned int draw_background : 1;
		unsigned int game_engine : 1;
	} options;

	/* Current rendering context */
	DRWContextState draw_ctx;

	/* Convenience pointer to text_store owned by the viewport */
	struct DRWTextStore **text_store_p;

	ListBase enabled_engines; /* RenderEngineType */

	bool buffer_finish_called; /* Avoid bad usage of DRW_render_instance_buffer_finish */

	/* View dependant uniforms. */
	DRWMatrixState original_mat; /* Original rv3d matrices. */
	int override_mat;            /* Bitflag of which matrices are overriden. */
	int num_clip_planes;         /* Number of active clipplanes. */
	bool dirty_mat;

	/* keep in sync with viewBlock */
	ViewUboStorage view_data;

	struct {
		float frustum_planes[6][4];
		BoundSphere frustum_bsphere;
		bool updated;
	} clipping;

#ifdef USE_GPU_SELECT
	unsigned int select_id;
#endif

	/* ---------- Nothing after this point is cleared after use ----------- */

	/* ogl_context serves as the offset for clearing only
	 * the top portion of the struct so DO NOT MOVE IT! */
	void *ogl_context;                /* Unique ghost context used by the draw manager. */
	Gwn_Context *gwn_context;
	ThreadMutex ogl_context_mutex;    /* Mutex to lock the drw manager and avoid concurent context usage. */

	/** GPU Resource State: Memory storage between drawing. */
	struct {
		GPUTexture **bound_texs;
		char *bound_tex_slots;
		int bind_tex_inc;
		GPUUniformBuffer **bound_ubos;
		char *bound_ubo_slots;
		int bind_ubo_inc;
	} RST;
} DRWManager;

extern DRWManager DST; /* TODO : get rid of this and allow multithreaded rendering */

/* --------------- FUNCTIONS ------------- */

void drw_texture_set_parameters(GPUTexture *tex, DRWTextureFlag flags);
void drw_texture_get_format(
        DRWTextureFormat format, bool is_framebuffer,
        GPUTextureFormat *r_data_type, int *r_channels, bool *r_is_depth);

void *drw_viewport_engine_data_ensure(void *engine_type);

void drw_state_set(DRWState state);

#endif /* __DRAW_MANAGER_H__ */
