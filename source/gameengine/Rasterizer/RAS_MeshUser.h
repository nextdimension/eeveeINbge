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
 * The Original Code is: all of this file.
 *
 * Contributor(s): Porteries Tristan.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file RAS_MeshUser.h
 *  \ingroup bgerast
 */

#ifndef __RAS_MESH_USER_H__
#define __RAS_MESH_USER_H__

#include "MT_Vector4.h"

class RAS_BoundingBox;

class RAS_MeshUser
{
private:
	/// OpenGL face wise.
	bool m_frontFace;
	/// Object color.
	MT_Vector4 m_color;
	/// Object transformation matrix.
	float m_matrix[16];
	/// Bounding box corresponding to a mesh or deformer.
	RAS_BoundingBox *m_boundingBox;
	/// Client object owner of this mesh user.
	void *m_clientObject;

public:
	RAS_MeshUser(void *clientobj, RAS_BoundingBox *boundingBox);
	virtual ~RAS_MeshUser();

	bool GetFrontFace() const;
	const MT_Vector4& GetColor() const;
	float *GetMatrix();
	RAS_BoundingBox *GetBoundingBox() const;
	void *GetClientObject() const;

	void SetFrontFace(bool frontFace);
	void SetColor(const MT_Vector4& color);
};

#endif  // __RAS_MESH_USER_H__
