#include "KX_MeshBuilder.h"
#include "KX_VertexProxy.h"
#include "KX_BlenderMaterial.h"
#include "KX_Scene.h"
#include "KX_KetsjiEngine.h"
#include "KX_Globals.h"
#include "KX_PyMath.h"

#include "EXP_ListWrapper.h"

#include "BL_BlenderConverter.h"

#include "RAS_BucketManager.h"

#include "BLI_math_vector.h"
#include "BLI_math_geom.h"

KX_MeshBuilderSlot::KX_MeshBuilderSlot(KX_BlenderMaterial *material, RAS_IDisplayArray::PrimitiveType primitiveType,
		const RAS_VertexFormat& format, unsigned int& origIndexCounter)
	:m_material(material),
	m_primitive(primitiveType),
	m_format(format),
	m_factory(RAS_IVertexFactory::Construct(m_format)),
	m_origIndexCounter(origIndexCounter)
{
}

KX_MeshBuilderSlot::KX_MeshBuilderSlot(RAS_MeshMaterial *meshmat, const RAS_VertexFormat& format, unsigned int& origIndexCounter)
	:m_format(format),
	m_factory(RAS_IVertexFactory::Construct(m_format)),
	m_origIndexCounter(origIndexCounter)
{
	RAS_MaterialBucket *bucket = meshmat->GetBucket();
	m_material = static_cast<KX_BlenderMaterial *>(bucket->GetPolyMaterial());

	RAS_IDisplayArray *array = meshmat->GetDisplayArray();
	m_primitive = array->GetPrimitiveType();

	m_vertexInfos = array->GetVertexInfoList();
	m_primitiveIndices = array->GetPrimitiveIndexList();
	m_triangleIndices = array->GetTriangleIndexList();

	for (unsigned int i = 0, size = array->GetVertexCount(); i < size; ++i) {
		m_vertices.push_back(m_factory->CopyVertex(array->GetVertexData(i)));
	}

	// Compute the maximum original index from the arrays.
	m_origIndexCounter = std::max(m_origIndexCounter, array->GetMaxOrigIndex());
}

KX_MeshBuilderSlot::~KX_MeshBuilderSlot()
{
}

std::string KX_MeshBuilderSlot::GetName()
{
	return m_material->GetName().substr(2);
}

KX_BlenderMaterial *KX_MeshBuilderSlot::GetMaterial() const
{
	return m_material;
}

void KX_MeshBuilderSlot::SetMaterial(KX_BlenderMaterial *material)
{
	m_material = material;
}

bool KX_MeshBuilderSlot::Invalid() const
{
	// WARNING: Always respect the order from RAS_DisplayArray::PrimitiveType.
	static const unsigned short itemsCount[] = {
		3, // TRIANGLES
		2 // LINES
	};

	const unsigned short count = itemsCount[m_primitive];
	return ((m_primitiveIndices.size() % count) != 0 ||
			(m_triangleIndices.size() % count) != 0);
}

RAS_IDisplayArray *KX_MeshBuilderSlot::GetDisplayArray() const
{
	return RAS_IDisplayArray::Construct(m_primitive, m_format, m_vertices, m_vertexInfos, m_primitiveIndices, m_triangleIndices);
}

PyTypeObject KX_MeshBuilderSlot::Type = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"KX_MeshBuilderSlot",
	sizeof(EXP_PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0, 0, 0, 0, 0, 0, 0,
	Methods,
	0,
	0,
	&EXP_Value::Type,
	0, 0, 0, 0, 0, 0,
	py_base_new
};

PyMethodDef KX_MeshBuilderSlot::Methods[] = {
	{"addVertex", (PyCFunction)KX_MeshBuilderSlot::sPyAddVertex, METH_VARARGS | METH_KEYWORDS},
	{"addIndex", (PyCFunction)KX_MeshBuilderSlot::sPyAddIndex, METH_O},
	{"removeVertex", (PyCFunction)KX_MeshBuilderSlot::sPyRemoveVertex, METH_VARARGS},
	{"addPrimitiveIndex", (PyCFunction)KX_MeshBuilderSlot::sPyAddPrimitiveIndex, METH_O},
	{"removePrimitiveIndex", (PyCFunction)KX_MeshBuilderSlot::sPyRemovePrimitiveIndex, METH_VARARGS},
	{"addTriangleIndex", (PyCFunction)KX_MeshBuilderSlot::sPyAddTriangleIndex, METH_O},
	{"removeTriangleIndex", (PyCFunction)KX_MeshBuilderSlot::sPyRemoveTriangleIndex, METH_VARARGS},
	{"recalculateNormals", (PyCFunction)KX_MeshBuilderSlot::sPyRecalculateNormals, METH_NOARGS},
	{nullptr, nullptr} // Sentinel
};

PyAttributeDef KX_MeshBuilderSlot::Attributes[] = {
	EXP_PYATTRIBUTE_RO_FUNCTION("vertices", KX_MeshBuilderSlot, pyattr_get_vertices),
	EXP_PYATTRIBUTE_RO_FUNCTION("indices", KX_MeshBuilderSlot, pyattr_get_indices),
	EXP_PYATTRIBUTE_RO_FUNCTION("triangleIndices", KX_MeshBuilderSlot, pyattr_get_triangleIndices),
	EXP_PYATTRIBUTE_RW_FUNCTION("material", KX_MeshBuilderSlot, pyattr_get_material, pyattr_set_material),
	EXP_PYATTRIBUTE_RO_FUNCTION("uvCount", KX_MeshBuilderSlot, pyattr_get_uvCount),
	EXP_PYATTRIBUTE_RO_FUNCTION("colorCount", KX_MeshBuilderSlot, pyattr_get_colorCount),
	EXP_PYATTRIBUTE_RO_FUNCTION("primitive", KX_MeshBuilderSlot, pyattr_get_primitive),
	EXP_PYATTRIBUTE_NULL // Sentinel
};

template <class ListType, ListType KX_MeshBuilderSlot::*List>
int KX_MeshBuilderSlot::get_size_cb(void *self_v)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return (self->*List).size();
}

PyObject *KX_MeshBuilderSlot::get_item_vertices_cb(void *self_v, int index)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	KX_VertexProxy *vert = new KX_VertexProxy(nullptr, RAS_Vertex(self->m_vertices[index], self->m_format));

	return vert->NewProxy(true);
}

template <RAS_IDisplayArray::IndexList KX_MeshBuilderSlot::*List>
PyObject *KX_MeshBuilderSlot::get_item_indices_cb(void *self_v, int index)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return PyLong_FromLong((self->*List)[index]);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_vertices(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return (new EXP_ListWrapper(self_v,
		self->GetProxy(),
		nullptr,
		get_size_cb<decltype(KX_MeshBuilderSlot::m_vertices), &KX_MeshBuilderSlot::m_vertices>,
		get_item_vertices_cb,
		nullptr,
		nullptr))->NewProxy(true);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_indices(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return (new EXP_ListWrapper(self_v,
		self->GetProxy(),
		nullptr,
		get_size_cb<decltype(KX_MeshBuilderSlot::m_primitiveIndices), &KX_MeshBuilderSlot::m_primitiveIndices>,
		get_item_indices_cb<&KX_MeshBuilderSlot::m_primitiveIndices>,
		nullptr,
		nullptr))->NewProxy(true);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_triangleIndices(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return (new EXP_ListWrapper(self_v,
		self->GetProxy(),
		nullptr,
		get_size_cb<decltype(KX_MeshBuilderSlot::m_triangleIndices), &KX_MeshBuilderSlot::m_triangleIndices>,
		get_item_indices_cb<&KX_MeshBuilderSlot::m_triangleIndices>,
		nullptr,
		nullptr))->NewProxy(true);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_material(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return self->GetMaterial()->GetProxy();
}

int KX_MeshBuilderSlot::pyattr_set_material(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef, PyObject *value)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	KX_BlenderMaterial *mat;
	if (ConvertPythonToMaterial(value, &mat, false, "slot.material = material; KX_MeshBuilderSlot excepted a KX_BlenderMaterial.")) {
		return PY_SET_ATTR_FAIL;
	}

	self->SetMaterial(mat);
	return PY_SET_ATTR_SUCCESS;
}

PyObject *KX_MeshBuilderSlot::pyattr_get_uvCount(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return PyBool_FromLong(self->GetDisplayArray()->GetFormat().uvSize);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_colorCount(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return PyBool_FromLong(self->GetDisplayArray()->GetFormat().colorSize);
}

PyObject *KX_MeshBuilderSlot::pyattr_get_primitive(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilderSlot *self = static_cast<KX_MeshBuilderSlot *>(self_v);
	return PyBool_FromLong(self->GetDisplayArray()->GetPrimitiveType());
}

PyObject *KX_MeshBuilderSlot::PyAddVertex(PyObject *args, PyObject *kwds)
{
	PyObject *pypos;
	PyObject *pynormal = nullptr;
	PyObject *pytangent = nullptr;
	PyObject *pyuvs = nullptr;
	PyObject *pycolors = nullptr;

	if (!EXP_ParseTupleArgsAndKeywords(args, kwds, "O|OOOO:addVertex",
			{"position", "normal", "tangent", "uvs", "colors", 0},
			&pypos, &pynormal, &pytangent, &pyuvs, &pycolors))
	{
		return nullptr;
	}

	mt::vec3 pos;
	if (!PyVecTo(pypos, pos)) {
		return nullptr;
	}

	mt::vec3 normal = mt::axisZ3;
	if (pynormal && !PyVecTo(pynormal, normal)) {
		return nullptr;
	}

	mt::vec4 tangent = mt::one4;
	if (pytangent && !PyVecTo(pytangent, tangent)) {
		return nullptr;
	}

	mt::vec2 uvs[RAS_Vertex::MAX_UNIT] = {mt::zero2};
	if (pyuvs) {
		if (!PySequence_Check(pyuvs)) {
			return nullptr;
		}

		const unsigned short size = max_ii(m_format.uvSize, PySequence_Size(pyuvs));
		for (unsigned short i = 0; i < size; ++i) {
			if (!PyVecTo(PySequence_GetItem(pyuvs, i), uvs[i])) {
				return nullptr;
			}
		}
	}

	unsigned int colors[RAS_Vertex::MAX_UNIT] = {0xFFFFFFFF};
	if (pycolors) {
		if (!PySequence_Check(pycolors)) {
			return nullptr;
		}

		const unsigned short size = max_ii(m_format.colorSize, PySequence_Size(pycolors));
		for (unsigned short i = 0; i < size; ++i) {
			mt::vec4 color;
			if (!PyVecTo(PySequence_GetItem(pycolors, i), color)) {
				return nullptr;
			}
			rgba_float_to_uchar(reinterpret_cast<unsigned char (&)[4]>(colors[i]), color.Data());
		}
	}

	RAS_IVertexData *vert = m_factory->CreateVertex(pos, uvs, tangent, colors, normal);
	m_vertices.push_back(vert);
	m_vertexInfos.emplace_back(m_origIndexCounter++, false);

	return PyLong_FromLong(m_vertices.size() - 1);
}

PyObject *KX_MeshBuilderSlot::PyAddIndex(PyObject *value)
{
	if (!PySequence_Check(value)) {
		PyErr_Format(PyExc_TypeError, "expected a list");
		return nullptr;
	}

	const bool isTriangle = (m_primitive == RAS_IDisplayArray::TRIANGLES);

	for (unsigned int i = 0, size = PySequence_Size(value); i < size; ++i) {
		const int val = PyLong_AsLong(PySequence_GetItem(value, i));

		if (val < 0 && PyErr_Occurred()) {
			PyErr_Format(PyExc_TypeError, "expected a list of positive integer");
			return nullptr;
		}

		m_primitiveIndices.push_back(val);
		if (isTriangle) {
			m_triangleIndices.push_back(val);
		}
	}

	Py_RETURN_NONE;
}

PyObject *KX_MeshBuilderSlot::PyAddPrimitiveIndex(PyObject *value)
{
	if (!PySequence_Check(value)) {
		PyErr_Format(PyExc_TypeError, "expected a list");
		return nullptr;
	}

	for (unsigned int i = 0, size = PySequence_Size(value); i < size; ++i) {
		const int val = PyLong_AsLong(PySequence_GetItem(value, i));

		if (val < 0 && PyErr_Occurred()) {
			PyErr_Format(PyExc_TypeError, "expected a list of positive integer");
			return nullptr;
		}

		m_primitiveIndices.push_back(val);
	}

	Py_RETURN_NONE;
}

PyObject *KX_MeshBuilderSlot::PyAddTriangleIndex(PyObject *value)
{
	if (!PySequence_Check(value)) {
		PyErr_Format(PyExc_TypeError, "expected a list");
		return nullptr;
	}

	for (unsigned int i = 0, size = PySequence_Size(value); i < size; ++i) {
		const int val = PyLong_AsLong(PySequence_GetItem(value, i));

		if (val < 0 && PyErr_Occurred()) {
			PyErr_Format(PyExc_TypeError, "expected a list of positive integer");
			return nullptr;
		}

		m_triangleIndices.push_back(val);
	}

	Py_RETURN_NONE;
}

template <class ListType>
static PyObject *removeDataCheck(ListType& list, int start, int end, const std::string& errmsg)
{
	const int size = list.size();
	if (start >= size || (end != -1 && (end > size || end < start))) {
		PyErr_Format(PyExc_TypeError, "%s: range invalid, must be included in [0, %i[", errmsg.c_str(), size);
		return nullptr;
	}

	if (end == -1) {
		list.erase(list.begin() + start);
	}
	else {
		list.erase(list.begin() + start, list.begin() + end);
	}

	Py_RETURN_NONE;
}

PyObject *KX_MeshBuilderSlot::PyRemoveVertex(PyObject *args)
{
	int start;
	int end = -1;

	if (!PyArg_ParseTuple(args, "i|i:removeVertex", &start, &end)) {
		return nullptr;
	}

	return removeDataCheck(m_vertices, start, end, "slot.removeVertex(start, end)");
}

PyObject *KX_MeshBuilderSlot::PyRemovePrimitiveIndex(PyObject *args)
{
	int start;
	int end = -1;

	if (!PyArg_ParseTuple(args, "i|i:removePrimitiveIndex", &start, &end)) {
		return nullptr;
	}

	return removeDataCheck(m_vertices, start, end, "slot.removePrimitiveIndex(start, end)");
}

PyObject *KX_MeshBuilderSlot::PyRemoveTriangleIndex(PyObject *args)
{
	int start;
	int end = -1;

	if (!PyArg_ParseTuple(args, "i|i:removeTriangleIndex", &start, &end)) {
		return nullptr;
	}

	return removeDataCheck(m_vertices, start, end, "slot.removeTriangleIndex(start, end)");
}

PyObject *KX_MeshBuilderSlot::PyRecalculateNormals()
{
	if (Invalid()) {
		PyErr_SetString(PyExc_TypeError, "slot.recalculateNormals(): slot has an invalid number of indices");
		return nullptr;
	}

	for (RAS_IVertexData *data : m_vertices) {
		zero_v3(data->normal);
	}

	for (unsigned int i = 0, size = m_primitiveIndices.size(); i < size; i += 3) {
		float normal[3];
		normal_tri_v3(normal,
				m_vertices[m_primitiveIndices[i]]->position,
				m_vertices[m_primitiveIndices[i + 1]]->position,
				m_vertices[m_primitiveIndices[i + 2]]->position);

		for (unsigned short j = 0; j < 3; ++j) {
			add_v3_v3(m_vertices[m_primitiveIndices[i + j]]->normal, normal);
		}
	}

	for (RAS_IVertexData *data : m_vertices) {
		normalize_v3(data->normal);
	}

	Py_RETURN_NONE;
}

KX_MeshBuilder::KX_MeshBuilder(const std::string& name, KX_Scene *scene, const RAS_Mesh::LayersInfo& layersInfo,
		const RAS_VertexFormat& format)
	:m_name(name),
	m_layersInfo(layersInfo),
	m_format(format),
	m_scene(scene),
	m_origIndexCounter(0)
{
}

KX_MeshBuilder::KX_MeshBuilder(const std::string& name, KX_Mesh *mesh)
	:m_name(name),
	m_layersInfo(mesh->GetLayersInfo()),
	m_scene(mesh->GetScene())
{
	m_format = {(uint8_t)max_ii(m_layersInfo.uvLayers.size(), 1), (uint8_t)max_ii(m_layersInfo.colorLayers.size(), 1)};

	for (RAS_MeshMaterial *meshmat : mesh->GetMeshMaterialList()) {
		KX_MeshBuilderSlot *slot = new KX_MeshBuilderSlot(meshmat, m_format, m_origIndexCounter);
		m_slots.Add(slot);
	}
}

KX_MeshBuilder::~KX_MeshBuilder()
{
}

std::string KX_MeshBuilder::GetName()
{
	return m_name;
}

EXP_ListValue<KX_MeshBuilderSlot>& KX_MeshBuilder::GetSlots()
{
	return m_slots;
}

static bool convertPythonListToLayers(PyObject *list, KX_Mesh::LayerList& layers, const std::string& errmsg)
{
	if (list == Py_None) {
		return true;
	}

	if (!PySequence_Check(list)) {
		PyErr_Format(PyExc_TypeError, "%s expected a list", errmsg.c_str());
		return false;
	}

	const unsigned short size = PySequence_Size(list);
	if (size > RAS_Vertex::MAX_UNIT) {
		PyErr_Format(PyExc_TypeError, "%s excepted a list of maximum %i items", errmsg.c_str(), RAS_Vertex::MAX_UNIT);
		return false;
	}

	for (unsigned short i = 0; i < size; ++i) {
		PyObject *value = PySequence_GetItem(list, i);

		if (!PyUnicode_Check(value)) {
			PyErr_Format(PyExc_TypeError, "%s excepted a list of string", errmsg.c_str());
		}

		const std::string name = std::string(_PyUnicode_AsString(value));
		layers.push_back({i, name});
	}

	return true;
}

static PyObject *py_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	const char *name;
	PyObject *pyscene;
	PyObject *pyuvs = Py_None;
	PyObject *pycolors = Py_None;

	if (!EXP_ParseTupleArgsAndKeywords(args, kwds, "sO|OO:KX_MeshBuilder",
			{"name", "scene", "uvs", "colors", 0}, &name, &pyscene, &pyuvs, &pycolors))
	{
		return nullptr;
	}

	KX_Scene *scene;
	if (!ConvertPythonToScene(pyscene, &scene, false, "KX_MeshBuilder(name, scene, uvs, colors): scene must be KX_Scene")) {
		return nullptr;
	}

	KX_Mesh::LayersInfo layersInfo;
	if (!convertPythonListToLayers(pyuvs, layersInfo.uvLayers, "KX_MeshBuilder(name, scene, uvs, colors): uvs:") ||
		!convertPythonListToLayers(pycolors, layersInfo.colorLayers, "KX_MeshBuilder(name, scene, uvs, colors): colors:"))
	{
		return nullptr;
	}

	RAS_VertexFormat format{(uint8_t)max_ii(layersInfo.uvLayers.size(), 1), (uint8_t)max_ii(layersInfo.colorLayers.size(), 1)};

	KX_MeshBuilder *builder = new KX_MeshBuilder(name, scene, layersInfo, format);

	return builder->NewProxy(true);
}

static PyObject *py_new_from_mesh(PyObject *UNUSED(self), PyObject *args)
{
	PyObject *pymesh;
	const char *name;

	if (!PyArg_ParseTuple(args, "sO:FromMesh", &name, &pymesh)) {
		return nullptr;
	}

	KX_Mesh *mesh;
	if (!ConvertPythonToMesh(KX_GetActiveScene()->GetLogicManager(), pymesh, &mesh, false, "KX_MeshBuilder.FromMesh")) {
		return nullptr;
	}

	KX_MeshBuilder *builder = new KX_MeshBuilder(name, mesh);

	return builder->NewProxy(true);
}

PyTypeObject KX_MeshBuilder::Type = {
	PyVarObject_HEAD_INIT(nullptr, 0)
	"KX_MeshBuilder",
	sizeof(EXP_PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0, 0, 0, 0, 0, 0, 0,
	Methods,
	0,
	0,
	&EXP_Value::Type,
	0, 0, 0, 0, 0, 0,
	py_new
};

PyMethodDef KX_MeshBuilder::Methods[] = {
	{"FromMesh", (PyCFunction)py_new_from_mesh, METH_STATIC | METH_VARARGS},
	{"addSlot", (PyCFunction)KX_MeshBuilder::sPyAddSlot, METH_VARARGS | METH_KEYWORDS},
	{"finish", (PyCFunction)KX_MeshBuilder::sPyFinish, METH_NOARGS},
	{nullptr, nullptr} // Sentinel
};

PyAttributeDef KX_MeshBuilder::Attributes[] = {
	EXP_PYATTRIBUTE_RO_FUNCTION("slots", KX_MeshBuilder, pyattr_get_slots),
	EXP_PYATTRIBUTE_NULL // Sentinel
};

PyObject *KX_MeshBuilder::pyattr_get_slots(EXP_PyObjectPlus *self_v, const EXP_PYATTRIBUTE_DEF *attrdef)
{
	KX_MeshBuilder *self = static_cast<KX_MeshBuilder *>(self_v);
	return self->GetSlots().GetProxy();
}

PyObject *KX_MeshBuilder::PyAddSlot(PyObject *args, PyObject *kwds)
{
	PyObject *pymat;
	int primitive;

	if (!EXP_ParseTupleArgsAndKeywords(args, kwds, "O|i:addSlot", {"material", "primitive", 0}, &pymat, &primitive)) {
		return nullptr;
	}

	KX_BlenderMaterial *material;
	if (!ConvertPythonToMaterial(pymat, &material, false, "meshBuilder.addSlot(...): material must be a KX_BlenderMaterial")) {
		return nullptr;
	}

	if (!ELEM(primitive, RAS_IDisplayArray::LINES, RAS_IDisplayArray::TRIANGLES)) {
		PyErr_SetString(PyExc_TypeError, "meshBuilder.addSlot(...): primitive value invalid");
		return nullptr;
	}

	KX_MeshBuilderSlot *slot = new KX_MeshBuilderSlot(material, (RAS_IDisplayArray::PrimitiveType)primitive, m_format, m_origIndexCounter);
	m_slots.Add(slot);

	return slot->GetProxy();
}

PyObject *KX_MeshBuilder::PyFinish()
{
	if (m_slots.GetCount() == 0) {
		PyErr_SetString(PyExc_TypeError, "meshBuilder.finish(): no mesh data found");
		return nullptr;
	}

	for (KX_MeshBuilderSlot *slot : m_slots) {
		if (slot->Invalid()) {
			PyErr_Format(PyExc_TypeError, "meshBuilder.finish(): slot (%s) has an invalid number of indices",
					slot->GetName().c_str());
			return nullptr;
		}
	}

	KX_Mesh *mesh = new KX_Mesh(m_scene, m_name, m_layersInfo);

	RAS_BucketManager *bucketManager = m_scene->GetBucketManager();
	for (unsigned short i = 0, size = m_slots.GetCount(); i < size; ++i) {
		KX_MeshBuilderSlot *slot = m_slots.GetValue(i);
		bool created;
		RAS_MaterialBucket *bucket = bucketManager->FindBucket(slot->GetMaterial(), created);
		mesh->AddMaterial(bucket, i, slot->GetDisplayArray());
	}

	mesh->EndConversion(m_scene->GetBoundingBoxManager());

	KX_GetActiveEngine()->GetConverter()->RegisterMesh(m_scene, mesh);

	return mesh->GetProxy();
}

