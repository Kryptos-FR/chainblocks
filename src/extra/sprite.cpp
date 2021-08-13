#include "blocks/shared.hpp"

namespace chainblocks {
namespace Sprite {

constexpr uint32_t SheetCC = 'shee';

struct Sheet {
  static inline Type ObjType{
      {CBType::Object, {.object = {.vendorId = CoreCC, .typeId = SheetCC}}}};
  static inline Type VarType = Type::VariableOf(ObjType);

  static inline ObjectVar<Sheet> Var{"Sprite-Sheet", CoreCC, SheetCC};

  static CBTypesInfo inputTypes() {
    // FIXME: do we need an input?
    return CoreInfo::AnyType;
  }

  static CBTypesInfo outputTypes() { return Sheet::ObjType; }

  static CBParametersInfo parameters() { return _params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _image = value;
      break;
    case 1:
      _atlas = value;
      break;

    default:
      throw CBException("Parameter out of range.");
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _image;
    case 1:
      return _atlas;
    }
    throw CBException("Parameter out of range.");
  }

  void cleanup() {
    _image.cleanup();
    _atlas.cleanup();
  }

  void warmup(CBContext *context) {
    _image.warmup(context);
    _atlas.warmup(context);
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    // read the atlas data
    if (_atlas.isVariable()) {
      // FIXME: no idea what to do. How to retrieve the actual value behind the
      // variable?
    } else {
      auto table = _atlas.get().payload.tableValue;
      auto api = table.api;

      this->_name = api->tableAt(table, "name")->payload.stringValue;

      auto size = api->tableAt(table, "size")->payload.seqValue;
      this->_width = size.elements[0].payload.intValue;
      this->_height = size.elements[1].payload.intValue;

      this->_format = api->tableAt(table, "format")->payload.stringValue;

      auto filter = api->tableAt(table, "filter")->payload.seqValue;
      this->_minFilter = filter.elements[0].payload.stringValue;
      this->_magFilter = filter.elements[1].payload.stringValue;

      auto repeat = api->tableAt(table, "repeat")->payload.seqValue;
      this->_u_repeat = repeat.elements[0].payload.stringValue;
      this->_v_repeat = repeat.elements[1].payload.stringValue;

      this->_premultiply = api->tableAt(table, "pma")->payload.boolValue;

      auto regions = api->tableAt(table, "regions")->payload.seqValue;
      this->_regions.reserve(regions.len);
      for (uint32_t i = 0; i < regions.len; ++i) {
        auto table = regions.elements[i].payload.tableValue;
        auto api = table.api;

        auto region = Region{};

        region._name = api->tableAt(table, "name")->payload.stringValue;
        region._index = api->tableAt(table, "index")->payload.intValue;
        region._rotation =
            api->tableAt(table, "rotation")->payload.boolValue ? 90 : 0;

        auto bounds = api->tableAt(table, "bounds")->payload.seqValue;
        for (uint32_t j = 0; j < bounds.len && j < 4; ++j) {
          region._bounds[j] = bounds.elements[j].payload.intValue;
        }

        auto offsets = api->tableAt(table, "offsets")->payload.seqValue;
        for (uint32_t j = 0; j < offsets.len && j < 4; ++j) {
          region._offsets[j] = offsets.elements[j].payload.intValue;
        }

        auto pad = api->tableAt(table, "pad")->payload.seqValue;
        for (uint32_t j = 0; j < pad.len && j < 4; ++j) {
          region._pad[j] = pad.elements[j].payload.intValue;
        }

        auto split = api->tableAt(table, "split")->payload.seqValue;
        for (uint32_t j = 0; j < split.len && j < 4; ++j) {
          region._split[j] = split.elements[j].payload.intValue;
        }

        this->_regions.push_back(region);
      }

      auto dummy = 0;
    }

    return Var::Object(this, CoreCC, SheetCC);
  }

private:
  static inline Parameters _params = {
      {"Image",
       CBCCSTR("The image to use with the atlas."),
       {CoreInfo::ImageType, CoreInfo::ImageVarType}},
      {"Atlas",
       CBCCSTR("The atlas definition."),
       {CoreInfo::AnyTableType, CoreInfo::AnyVarTableType}}};

  // params
  ParamVar _atlas{};
  ParamVar _image{};

  struct Region {
    std::string _name; // FIXME: might not be needed (or only for debug)
    int16_t _index;    // FIXME: might not be needed (or only for debug)
    uint16_t _rotation;
    uint16_t _bounds[4];
    uint16_t _offsets[4];
    int16_t _pad[4];
    uint16_t _split[4];
  };

  std::string _name; // FIXME: might not be needed (or only for debug)
  uint16_t _width;
  uint16_t _height;
  std::string _format;
  std::string _minFilter;
  std::string _magFilter;
  std::string _u_repeat;
  std::string _v_repeat;
  bool _premultiply;
  std::vector<Region> _regions;
};

struct Draw {

  static CBTypesInfo inputTypes() { return Sheet::ObjType; }

  static CBTypesInfo outputTypes() {
    // FIXME: output is a texture (or an image)?
    return CoreInfo::AnyType;
  }

  static CBParametersInfo parameters() { return _params; }

  CBVar getParam(int index) {
    // FIXME
    return Var::Empty;
  }

  void setParam(int index, const CBVar &value) {
    // FIXME
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    CBVar result{};
    // TODO: build the texture based on the sheet image, sheet data and the
    // range (i.e. sprite region indices)
    return result;
  }

private:
  static inline Parameters _params = {
      {"Range", CBCCSTR("TODO"), {CoreInfo::Int2Type, CoreInfo::Int2VarType}},
      {"Repeat", CBCCSTR("TODO"), {CoreInfo::BoolType, CoreInfo::BoolVarType}},
      {"Speed", CBCCSTR("TODO"), {CoreInfo::FloatType, CoreInfo::FloatVarType}},
      {"PlayFromStart",
       CBCCSTR("TODO"),
       {CoreInfo::BoolType, CoreInfo::BoolVarType}}};
};

void registerBlocks() {
  REGISTER_CBLOCK("Sprite.Draw", Draw);
  REGISTER_CBLOCK("Sprite.Sheet", Sheet);
}

}; // namespace Sprite
}; // namespace chainblocks
