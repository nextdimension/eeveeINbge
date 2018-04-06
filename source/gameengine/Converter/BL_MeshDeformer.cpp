/*
 * ***** BEGIN GPL LICENSE BLOCK *****
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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 * Simple deformation controller that restores a mesh to its rest position
 */

/** \file gameengine/Converter/BL_MeshDeformer.cpp
 *  \ingroup bgeconv
 */

#ifdef _MSC_VER
/* This warning tells us about truncation of __long__ stl-generated names.
 * It can occasionally cause DevStudio to have internal compiler warnings. */
#  pragma warning( disable:4786 )
#endif

#include "RAS_IPolygonMaterial.h"
#include "RAS_DisplayArray.h"
#include "BL_DeformableGameObject.h"
#include "BL_MeshDeformer.h"
#include "RAS_MeshObject.h"
#include "RAS_Polygon.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include <string>
#include "BLI_math.h"

bool BL_MeshDeformer::Apply(RAS_MeshMaterial *UNUSED(meshmat), RAS_IDisplayArray *UNUSED(array))
{
	// only apply once per frame if the mesh is actually modified
	if (m_lastDeformUpdate != m_gameobj->GetLastFrame()) {
		// For each display array
		for (RAS_IDisplayArray *array: m_displayArrayList) {
			if (array->GetModifiedFlag() == RAS_IDisplayArray::NONE_MODIFIED) {
				continue;
			}

			//	For each vertex
			for (unsigned int i = 0, size = array->GetVertexCount(); i < size; ++i) {
				RAS_ITexVert *v = array->GetVertex(i);
				const RAS_TexVertInfo& vinfo = array->GetVertexInfo(i);
				v->SetXYZ(m_bmesh->mvert[vinfo.getOrigIndex()].co);
			}

			array->SetModifiedFlag(RAS_IDisplayArray::POSITION_MODIFIED);
		}

		m_lastDeformUpdate = m_gameobj->GetLastFrame();

		return true;
	}

	return false;
}

BL_MeshDeformer::BL_MeshDeformer(BL_DeformableGameObject *gameobj, Object *obj, RAS_MeshObject *meshobj)
	:RAS_Deformer(meshobj),
	m_bmesh((Mesh *)(obj->data)),
	m_transverts(nullptr),
	m_transnors(nullptr),
	m_objMesh(obj),
	m_tvtot(0),
	m_gameobj(gameobj),
	m_lastDeformUpdate(-1.0)
{
	KX_Scene *scene = m_gameobj->GetScene();
}

BL_MeshDeformer::~BL_MeshDeformer()
{
	if (m_transverts)
		delete[] m_transverts;
	if (m_transnors)
		delete[] m_transnors;
}

void BL_MeshDeformer::ProcessReplica()
{
	RAS_Deformer::ProcessReplica();
	m_transverts = nullptr;
	m_transnors = nullptr;
	m_tvtot = 0;
	m_bDynamic = false;
	m_lastDeformUpdate = -1.0;
}

void BL_MeshDeformer::Relink(std::map<SCA_IObject *, SCA_IObject *>& map)
{
	m_gameobj = static_cast<BL_DeformableGameObject *>(map[m_gameobj]);
}

/**
 * \warning This function is expensive!
 */
void BL_MeshDeformer::RecalcNormals()
{
	// if we don't use a vertex array we does nothing.
	if (!UseVertexArray()) {
		return;
	}

	/* We don't normalize for performance, not doing it for faces normals
	 * gives area-weight normals which often look better anyway, and use
	 * GL_NORMALIZE so we don't have to do per vertex normalization either
	 * since the GPU can do it faster */

	/* set vertex normals to zero */
	memset(m_transnors, 0, sizeof(float) * 3 * m_bmesh->totvert);

	for (unsigned int i = 0, numpoly = m_mesh->NumPolygons(); i < numpoly; ++i) {
		RAS_Polygon *poly = m_mesh->GetPolygon(i);
		RAS_IDisplayArray *array = poly->GetDisplayArray();
		const unsigned short numvert = poly->VertexCount();

		const float *co[4];
		unsigned int indices[4];
		unsigned int origindices[4];
		bool flat = true;

		for (unsigned int j = 0; j < numvert; ++j) {
			const unsigned int index = poly->GetVertexOffset(j);
			const RAS_TexVertInfo& vinfo = array->GetVertexInfo(index);
			const unsigned int origindex = vinfo.getOrigIndex();

			co[j] = m_transverts[origindex];
			indices[j] = index;
			origindices[j] = origindex;

			if (!(vinfo.getFlag() & RAS_TexVertInfo::FLAT)) {
				flat = false;
			}
		}

		float pnorm[3];
		if (numvert == 3) {
			normal_tri_v3(pnorm, co[0], co[1], co[2]);
		}
		else {
			normal_quad_v3(pnorm, co[0], co[1], co[2], co[3]);
		}

		if (flat) {
			MT_Vector3 normal(pnorm);
			for (unsigned int j = 0; j < numvert; ++j) {
				RAS_ITexVert *vert = array->GetVertex(indices[j]);

				vert->SetNormal(normal);
			}
		}
		else {
			for (unsigned int j = 0; j < numvert; ++j) {
				add_v3_v3(m_transnors[origindices[j]], pnorm);
			}
		}
	}

	// Assign smooth vertex normals.
	for (RAS_IDisplayArray *array: m_displayArrayList) {
		for (unsigned int i = 0, size = array->GetVertexCount(); i < size; ++i) {
			RAS_ITexVert *v = array->GetVertex(i);
			const RAS_TexVertInfo& vinfo = array->GetVertexInfo(i);

			if (!(vinfo.getFlag() & RAS_TexVertInfo::FLAT))
				v->SetNormal(MT_Vector3(m_transnors[vinfo.getOrigIndex()])); //.safe_normalized()
		}
	}
}

void BL_MeshDeformer::VerifyStorage()
{
	/* Ensure that we have the right number of verts assigned */
	if (m_tvtot != m_bmesh->totvert) {
		if (m_transverts)
			delete[] m_transverts;
		if (m_transnors)
			delete[] m_transnors;

		m_transverts = new float[m_bmesh->totvert][3];
		m_transnors = new float[m_bmesh->totvert][3];
		m_tvtot = m_bmesh->totvert;
	}

	for (unsigned int v = 0; v < m_bmesh->totvert; v++) {
		copy_v3_v3(m_transverts[v], m_bmesh->mvert[v].co);
		normal_short_to_float_v3(m_transnors[v], m_bmesh->mvert[v].no);
	}
}

