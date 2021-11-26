/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2021 Fragcolor Pte. Ltd. */

#include "blocks/shared.hpp"
#include "linalg_shim.hpp"

using namespace chainblocks;

namespace chainblocks {
// TODO! should we name it "Shape" instead?
namespace Procedural {

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

void registerBlocks() {
  REGISTER_CBLOCK("Shape.Cube", Cube);
}
}; // namespace Procedural
}; // namespace chainblocks
