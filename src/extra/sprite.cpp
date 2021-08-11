#include "blocks/shared.hpp"

namespace chainblocks {
namespace Sprite {

struct Draw {

  static CBTypesInfo inputTypes() {
    // FIXME
    return CoreInfo::AnyType;
  }

  static CBTypesInfo outputTypes() {
    // FIXME
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
    return result;
  }

private:
  static inline Parameters _params = {

  };
};

struct AtlasPage : TableVar {
  AtlasPage() : TableVar(), regions(get<SeqVar>(":region")) {}

  AtlasPage(const CBVar &other) : AtlasPage() {
    chainblocks::cloneVar(*this, other);
    regions = get<SeqVar>(":region");
  }

  SeqVar &regions;
};

struct Sheet {

  static CBTypesInfo inputTypes() {
    // FIXME
    return CoreInfo::AnyType;
  }

  static CBTypesInfo outputTypes() {
    // FIXME: need to return our data into a variable
    return CoreInfo::AnyType;
  }

  static CBParametersInfo parameters() { return _params; }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      // FIXME
      //return Var(_images);
      return Var::Empty;
    case 1:
      return Var(_pages);
    }
    throw CBException("Parameter out of range.");
  }

  void setParam(int index, const CBVar &value) {
    if (value.valueType == CBType::None)
      return;

    switch (index) {
    case 0:
      switch (value.valueType) {
      case CBType::Seq: {
        auto count = value.payload.seqValue.len;
        if (count == 0)
          break;

        _images.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
          auto element = &value.payload.seqValue.elements[i];
          _images.push_back(element->payload.imageValue);
        }
      } break;

      case CBType::Image:
        _images.reserve(1);
        _images.push_back(value.payload.imageValue);
        break;

      default:
        break;
      }
      break;

    case 1: {
      switch (value.valueType) {
      case CBType::Seq: {
        auto count = value.payload.seqValue.len;
        if (count == 0)
          break;

        _pages.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
          auto element = &value.payload.seqValue.elements[i];
          addPage(element->payload.tableValue);
        }
      } break;

      case CBType::Table: {
        _pages.reserve(1);
        addPage(value.payload.tableValue);
      } break;

      default:
        break;
      }
      break;
    }
    
    default:
      break;
    }
  }

  void addPage(const CBTable &table) {
    auto page = AtlasPage(*(CBVar *)&table); // UGLY
    _pages.push_back(page);
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    CBVar result{};
    return result;
  }

private:
  static inline Parameters _params = {
      // TODO allow ImageVarType, ImageSeqType and ImageVarSeqType
      {"Image",
       CBCCSTR("The image to use with the atlas."),
       {CoreInfo::ImageSeqType, CoreInfo::ImageType}},
      {"Atlas",
       CBCCSTR("The atlas definition."),
       {CoreInfo::AnySeqType, CoreInfo::AnyTableType}}};

  std::vector<CBImage> _images;
  std::vector<AtlasPage> _pages;
};

void registerBlocks() {
  REGISTER_CBLOCK("Sprite.Draw", Draw);
  REGISTER_CBLOCK("Sprite.Sheet", Sheet);
}

}; // namespace Sprite
}; // namespace chainblocks
