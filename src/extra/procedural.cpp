/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2021 Fragcolor Pte. Ltd. */

#include "blocks/shared.hpp"
#include "linalg_shim.hpp"

using namespace chainblocks;

namespace chainblocks {
// TODO! should we name it "Shape" instead?
namespace Procedural {
const double PI = 3.141592653589793238463;

// note: copied from bgfx.cpp, should be shared at some point
static inline Types VerticesSeqTypes{{CoreInfo::FloatType, CoreInfo::Float2Type,
                                      CoreInfo::Float3Type, CoreInfo::ColorType,
                                      CoreInfo::IntType}};
static inline Type VerticesSeq = Type::SeqOf(VerticesSeqTypes);
// TODO support other topologies than triangle list
static inline Types IndicesSeqTypes{{CoreInfo::Int3Type}};
static inline Type IndicesSeq = Type::SeqOf(IndicesSeqTypes);
static inline Types TableTypes{{VerticesSeq, IndicesSeq}};
static inline std::array<CBString, 2> TableKeys{"Vertices", "Indices"};
static inline Type Table = Type::TableOf(TableTypes, TableKeys);

struct Base {
  static CBTypesInfo inputTypes() { return CoreInfo::NoneType; }

  static CBTypesInfo outputTypes() { return Table; }

protected:
  std::vector<Var> _indices;
  std::vector<Var> _vertices;
  TableVar _outputTable{};
};

struct Cube : public Base {
  CBVar activate(CBContext *context, const CBVar &input) {
    _indices.resize(12);
    _vertices.resize(8);

    populateIndices(_indices);
    populateVertices(_vertices);

    _outputTable["Indices"] = Var(_indices);
    _outputTable["Vertices"] = Var(_vertices);
    return _outputTable;
  }

private:
  void populateIndices(std::vector<Var> &indices) {
    uint32_t i = 0;
    indices[i++] = Var(0, 1, 2);
    indices[i++] = Var(1, 3, 2);
    indices[i++] = Var(4, 6, 5);
    indices[i++] = Var(5, 6, 7);
    indices[i++] = Var(0, 2, 4);
    indices[i++] = Var(4, 2, 6);
    indices[i++] = Var(1, 5, 3);
    indices[i++] = Var(5, 7, 3);
    indices[i++] = Var(0, 4, 1);
    indices[i++] = Var(4, 5, 1);
    indices[i++] = Var(2, 3, 6);
    indices[i++] = Var(6, 3, 7);
  }

  void populateVertices(std::vector<Var> &vertices) {
    uint32_t i = 0;
    vertices[i++] = Var(-1.0f, +1.0f, +1.0f);
    vertices[i++] = Var(+1.0f, +1.0f, +1.0f);
    vertices[i++] = Var(-1.0f, -1.0f, +1.0f);
    vertices[i++] = Var(+1.0f, -1.0f, +1.0f);
    vertices[i++] = Var(-1.0f, +1.0f, -1.0f);
    vertices[i++] = Var(+1.0f, +1.0f, -1.0f);
    vertices[i++] = Var(-1.0f, -1.0f, -1.0f);
    vertices[i++] = Var(+1.0f, -1.0f, -1.0f);
  }
};

struct Cylinder : public Base {

  static CBParametersInfo parameters() { return _params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _tessellation = value;
      break;
    case 1:
      _height = value;
      break;
    case 2:
      _radius = value;
      break;

    default:
      throw CBException("Parameter out of range.");
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _tessellation;
    case 1:
      return _height;
    case 2:
      return _radius;

    default:
      throw CBException("Parameter out of range.");
    }
  }

  void cleanup() {
    _tessellation.cleanup();
    _height.cleanup();
    _radius.cleanup();
  }

  void warmup(CBContext *context) {
    _tessellation.warmup(context);
    _height.warmup(context);
    _radius.warmup(context);
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    // params
    float height = _height.get().payload.floatValue;
    float radius = _radius.get().payload.floatValue;
    int tessellation = _tessellation.get().payload.intValue;

    if (tessellation < 3)
      tessellation = 3;

    _indices.clear();
    _vertices.clear();
    _indices.reserve(4 * tessellation - 2);
    _vertices.reserve(4 * tessellation + 2);

    height /= 2;

    auto topOffset = linalg::aliases::float3{0.0f, height, 0.0f};

    auto stride = tessellation + 1;

    // Create a ring of triangles around the outside of the cylinder.
    for (auto i = 0; i <= tessellation; i++) {
      auto normal = circleVector(i, tessellation);

      auto sideOffset = linalg::cmul(normal, radius);
      auto plus = sideOffset + topOffset;
      auto minus = sideOffset - topOffset;

      _vertices.push_back(Var(plus.x, plus.y, plus.z));
      _vertices.push_back(Var(minus.x, minus.y, minus.z));

      _indices.push_back(Var(i * 2, (i * 2 + 2) % (stride * 2), i * 2 + 1));
      _indices.push_back(Var(i * 2 + 1, (i * 2 + 2) % (stride * 2),
                             (i * 2 + 3) % (stride * 2)));
    }

    // Create flat triangle fan caps to seal the top and bottom.
    cylinderCap(_indices, _vertices, tessellation, height, radius, true);
    cylinderCap(_indices, _vertices, tessellation, height, radius, false);

    _outputTable["Indices"] = Var(_indices);
    _outputTable["Vertices"] = Var(_vertices);
    return _outputTable;
  }

private:
  const linalg::aliases::float3 circleVector(int i, int tessellation) {
    auto angle = (i * 2.0f * PI / tessellation);
    auto dx = __builtin_sinf(angle);
    auto dz = __builtin_cosf(angle);

    return linalg::aliases::float3{dx, 0.0f, dz};
  }

  void cylinderCap(std::vector<Var> &indices, std::vector<Var> &vertices,
                   int tessellation, float height, float radius, bool isTop) {
    // Create cap indices.
    for (int i = 0; i < tessellation - 2; i++) {
      int i1 = (i + 1) % tessellation;
      int i2 = (i + 2) % tessellation;

      if (isTop) {
        std::swap(i1, i2);
      }

      int vbase = vertices.size();
      indices.push_back(Var(vbase, vbase + i1, vbase + i2));
    }

    // Which end of the cylinder is this?
    auto normal = linalg::aliases::float3{0.0f, 1.0f, 0.0f};

    if (!isTop) {
      normal = -normal;
    }

    // Create cap vertices.
    for (int i = 0; i < tessellation; i++) {
      auto circle = circleVector(i, tessellation);
      auto position = (circle * radius) + (normal * height);

      vertices.push_back(Var(position.x, position.y, position.z));
    }
  }

  static inline Parameters _params = {
      {"Tessellation",
       CBCCSTR("TODO!"),
       {CoreInfo::IntType, CoreInfo::IntVarType}},
      {"Height",
       CBCCSTR("TODO!"),
       {CoreInfo::FloatType, CoreInfo::FloatVarType}},
      {"Radius",
       CBCCSTR("TODO!"),
       {CoreInfo::FloatType, CoreInfo::FloatVarType}},
  };

  ParamVar _height{Var(1.0f)};
  ParamVar _radius{Var(0.5f)};
  ParamVar _tessellation{Var(8)};
};

struct Sphere : public Base {
  static CBParametersInfo parameters() { return _params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _tessellation = value;
      break;
    case 1:
      _radius = value;
      break;

    default:
      throw CBException("Parameter out of range.");
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _tessellation;
    case 1:
      return _radius;

    default:
      throw CBException("Parameter out of range.");
    }
  }

  void cleanup() {
    _tessellation.cleanup();
    _radius.cleanup();
  }

  void warmup(CBContext *context) {
    _tessellation.warmup(context);
    _radius.warmup(context);
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    // params
    float radius = _radius.get().payload.floatValue;
    int tessellation = _tessellation.get().payload.intValue;

    if (tessellation < 3)
      tessellation = 3;

    auto verticalSegments = tessellation;
    auto horizontalSegments = tessellation * 2;

    _indices.resize(verticalSegments * (horizontalSegments + 1) * 2);
    _vertices.resize((verticalSegments + 1) * (horizontalSegments + 1));

    auto vertexCount = 0;

    // generate the first extremity points
    for (auto j = 0; j <= horizontalSegments; j++) {
      auto normal = linalg::aliases::float3{0.0f, -1.0f, 0.0f};
      auto vertex = normal * radius;
      _vertices[vertexCount++] = Var(vertex.x, vertex.y, vertex.z);
    }

    // Create rings of vertices at progressively higher latitudes.
    for (auto i = 1; i < verticalSegments; i++) {
      auto latitude = float((i * PI / verticalSegments) - PI / 2.0);
      auto dy = __builtin_sinf(latitude);
      auto dxz = __builtin_cosf(latitude);

      // the first point
      auto firstNormal = linalg::aliases::float3{0, dy, dxz};
      auto firstHVertex = firstNormal * radius;
      _vertices[vertexCount++] =
          Var(firstHVertex.x, firstHVertex.y, firstHVertex.z);

      // Create a single ring of vertices at this latitude.
      for (auto j = 1; j < horizontalSegments; j++) {
        auto longitude = float(j * 2.0 * PI / horizontalSegments);
        auto dx = __builtin_sinf(longitude);
        auto dz = __builtin_cosf(longitude);

        dx *= dxz;
        dz *= dxz;

        auto normal = linalg::aliases::float3{dx, dy, dz};
        auto vertex = normal * radius;
        _vertices[vertexCount++] = Var(vertex.x, vertex.y, vertex.z);
      }

      // the last point equal to the first point
      _vertices[vertexCount++] =
          Var(firstHVertex.x, firstHVertex.y, firstHVertex.z);
    }

    // generate the end extremity points
    for (auto j = 0; j <= horizontalSegments; j++) {
      auto normal = linalg::aliases::float3{0.0f, 1.0f, 0.0f};
      auto vertex = normal * radius;
      _vertices[vertexCount++] = Var(vertex.x, vertex.y, vertex.z);
    }

    // Fill the index buffer with triangles joining each pair of latitude rings.
    auto stride = horizontalSegments + 1;

    auto indexCount = 0;
    for (auto i = 0; i < verticalSegments; i++) {
      for (auto j = 0; j <= horizontalSegments; j++) {
        auto nextI = i + 1;
        auto nextJ = (j + 1) % stride;

        _indices[indexCount++] =
            Var(i * stride + j, nextI * stride + j, i * stride + nextJ);
        _indices[indexCount++] =
            Var(i * stride + nextJ, nextI * stride + j, nextI * stride + nextJ);
      }
    }

    _outputTable["Indices"] = Var(_indices);
    _outputTable["Vertices"] = Var(_vertices);
    return _outputTable;
  }

private:
  static inline Parameters _params = {
      {"Tessellation",
       CBCCSTR("TODO!"),
       {CoreInfo::IntType, CoreInfo::IntVarType}},
      {"Radius",
       CBCCSTR("TODO!"),
       {CoreInfo::FloatType, CoreInfo::FloatVarType}},
  };

  ParamVar _radius{Var(0.5f)};
  ParamVar _tessellation{Var(8)};
};

void registerBlocks() {
  REGISTER_CBLOCK("Shape.Cube", Cube);
  REGISTER_CBLOCK("Shape.Cylinder", Cylinder);
  REGISTER_CBLOCK("Shape.Sphere", Sphere);
}
}; // namespace Procedural
}; // namespace chainblocks
