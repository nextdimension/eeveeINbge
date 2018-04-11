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
 */

/** \file gameengine/Ketsji/KX_FontObject.cpp
 *  \ingroup ketsji
 */

#include "EXP_StringValue.h"

#include "KX_FontObject.h"
#include "KX_Globals.h"
#include "KX_PyMath.h"
#include "KX_Scene.h"

#include "RAS_BucketManager.h"

extern "C" {
#  include "BLI_blenlib.h"
#  include "BKE_font.h"
#  include "depsgraph/DEG_depsgraph_query.h"
#  include "DNA_curve_types.h"
#  include "DNA_vfont_types.h"
#  include "MEM_guardedalloc.h"
}

#include "CM_Message.h"

static std::vector<std::string> split_string(std::string str)
{
	std::vector<std::string> text = std::vector<std::string>();

	// Split the string upon new lines
	int begin = 0, end = 0;
	while (end < str.size()) {
		if (str[end] == '\n') {
			text.push_back(str.substr(begin, end - begin));
			begin = end + 1;
		}
		end++;
	}
	// Now grab the last line
	text.push_back(str.substr(begin, end - begin));

	return text;
}

KX_FontObject::KX_FontObject(void *sgReplicationInfo, SG_Callbacks callbacks, Object *ob)
	:KX_GameObject(sgReplicationInfo, callbacks)
{
	Curve *text = static_cast<Curve *> (ob->data);

	SetText(text->str);

	/* We do a backup from original text to restore it at ge exit */
	m_backupText = text->str;
}

KX_FontObject::~KX_FontObject()
{
	//remove font from the scene list
	//it's handled in KX_Scene::NewRemoveObject
	SetCurveFromString(m_backupText);
}

CValue *KX_FontObject::GetReplica()
{
	KX_FontObject *replica = new KX_FontObject(*this);
	replica->ProcessReplica();
	return replica;
}

void KX_FontObject::ProcessReplica()
{
	KX_GameObject::ProcessReplica();
}

void KX_FontObject::AddMeshReadOnlyDisplayArray() // For text physics I guess
{
	RAS_BucketManager *bucketManager = GetScene()->GetBucketManager();
	RAS_DisplayArrayBucket *arrayBucket = bucketManager->GetTextDisplayArrayBucket();
}

void KX_FontObject::SetText(const std::string& text)
{
	m_text = text;
	m_texts = split_string(text);
}

void KX_FontObject::SetCurveFromString(const std::string text)
{
	Object *ob = GetBlenderObject();
	Curve *cu = (Curve *)ob->data;

	size_t len_bytes;
	size_t len_chars = BLI_strlen_utf8_ex(text.c_str(), &len_bytes);

	cu->len_wchar = len_chars;
	cu->len = len_bytes;
	cu->pos = len_chars;

	if (cu->str)
		MEM_freeN(cu->str);
	if (cu->strinfo)
		MEM_freeN(cu->strinfo);

	cu->str = (char *)MEM_mallocN(len_bytes + sizeof(wchar_t), "str");
	cu->strinfo = (CharInfo *)MEM_callocN((len_chars + 4) * sizeof(CharInfo), "strinfo");

	BLI_strncpy(cu->str, text.c_str(), len_bytes + 1);

	DEG_id_tag_update(&ob->id, OB_RECALC_DATA);
	GetScene()->ResetTaaSamples();
}

void KX_FontObject::UpdateTextFromProperty()
{
	// Allow for some logic brick control
	CValue *prop = GetProperty("Text");
	if (prop && prop->GetText() != m_text) {
		std::string text = prop->GetText();
		SetText(text);
		SetCurveFromString(text);
	}
}

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* Python Integration Hooks					                                 */
/* ------------------------------------------------------------------------- */

PyTypeObject KX_FontObject::Type = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"KX_FontObject",
	sizeof(PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0,
	&KX_GameObject::Sequence,
	&KX_GameObject::Mapping,
	0, 0, 0,
	nullptr,
	nullptr,
	0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0, 0, 0, 0, 0, 0, 0,
	Methods,
	0,
	0,
	&KX_GameObject::Type,
	0, 0, 0, 0, 0, 0,
	py_base_new
};

PyMethodDef KX_FontObject::Methods[] = {
	{nullptr, nullptr} //Sentinel
};

PyAttributeDef KX_FontObject::Attributes[] = {
	KX_PYATTRIBUTE_RW_FUNCTION("text", KX_FontObject, pyattr_get_text, pyattr_set_text),
	KX_PYATTRIBUTE_NULL    //Sentinel
};

PyObject *KX_FontObject::pyattr_get_text(PyObjectPlus *self_v, const KX_PYATTRIBUTE_DEF *attrdef)
{
	KX_FontObject *self = static_cast<KX_FontObject *>(self_v);
	return PyUnicode_FromStdString(self->m_text);
}

int KX_FontObject::pyattr_set_text(PyObjectPlus *self_v, const KX_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_FontObject *self = static_cast<KX_FontObject *>(self_v);
	if (!PyUnicode_Check(value))
		return PY_SET_ATTR_FAIL;
	char *chars = _PyUnicode_AsString(value);

	/* Allow for some logic brick control */
	CValue *tprop = self->GetProperty("Text");
	if (tprop) {
		CValue *newstringprop = new CStringValue(std::string(chars), "Text");
		self->SetProperty("Text", newstringprop);
		newstringprop->Release();
	}
	else {
		self->SetText(std::string(chars));
	}

	return PY_SET_ATTR_SUCCESS;
}

#endif // WITH_PYTHON
