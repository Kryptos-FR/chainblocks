/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019 Giovanni Petrantoni */

#include "../runtime.hpp"
#include "../utility.hpp"

namespace chainblocks {
struct JointOp {
  std::vector<CBVar *> _multiSortColumns;
  CBVar _columns{};

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anySeqInfo); }
  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anySeqInfo); }

  static inline ParamsInfo joinOpParams = ParamsInfo(ParamsInfo::Param(
      "Join",
      "Other columns to join sort/filter using the input (they must be "
      "of the same length).",
      CBTypesInfo(CoreInfo::varSeqInfo)));

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      cloneVar(_columns, value);
      // resets vars fetch in activate
      _multiSortColumns.clear();
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _columns;
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }

  void cleanup() { _multiSortColumns.clear(); }

  void ensureJoinSetup(CBContext *context, const CBVar &input) {
    if (_columns.valueType != None) {
      auto len = stbds_arrlen(input.payload.seqValue);
      if (_multiSortColumns.size() == 0) {
        if (_columns.valueType == Seq) {
          auto seq = IterableSeq(_columns.payload.seqValue);
          for (const auto &col : seq) {
            auto target = contextVariable(context, col.payload.stringValue);
            if (target && target->valueType == Seq) {
              auto mseqLen = stbds_arrlen(target->payload.seqValue);
              if (len != mseqLen) {
                throw CBException(
                    "JointOp: All the sequences to be processed must have "
                    "the same length as the input sequence.");
              }
              _multiSortColumns.push_back(target);
            }
          }
        } else if (_columns.valueType ==
                   ContextVar) { // normal single context var
          auto target = contextVariable(context, _columns.payload.stringValue);
          if (target && target->valueType == Seq) {
            auto mseqLen = stbds_arrlen(target->payload.seqValue);
            if (len != mseqLen) {
              throw CBException(
                  "JointOp: All the sequences to be processed must have "
                  "the same length as the input sequence.");
            }
            _multiSortColumns.push_back(target);
          }
        }
      } else {
        for (const auto &seqVar : _multiSortColumns) {
          const auto &seq = seqVar->payload.seqValue;
          auto mseqLen = stbds_arrlen(seq);
          if (len != mseqLen) {
            throw CBException(
                "JointOp: All the sequences to be processed must have "
                "the same length as the input sequence.");
          }
        }
      }
    }
  }
};

struct Sort : public JointOp {
  std::vector<CBVar> _multiSortKeys;

  bool _desc = false;

  static inline ParamsInfo paramsInfo = ParamsInfo(
      joinOpParams,
      ParamsInfo::Param(
          "Desc",
          "If sorting should be in descending order, defaults ascending.",
          CBTypesInfo(CoreInfo::boolInfo)));

  static CBParametersInfo parameters() { return CBParametersInfo(paramsInfo); }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      return JointOp::setParam(index, value);
    case 1:
      _desc = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return JointOp::getParam(index);
    case 1:
      return Var(_desc);
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }

  struct {
    bool operator()(CBVar &a, CBVar &b) const { return a > b; }
  } sortAsc;

  struct {
    bool operator()(CBVar &a, CBVar &b) const { return a < b; }
  } sortDesc;

  template <class Compare> void insertSort(CBVar seq[], int n, Compare comp) {
    int i, j;
    CBVar key{};
    for (i = 1; i < n; i++) {
      key = seq[i];
      _multiSortKeys.clear();
      for (const auto &seqVar : _multiSortColumns) {
        const auto &col = seqVar->payload.seqValue;
        _multiSortKeys.push_back(col[i]);
      }
      j = i - 1;
      while (j >= 0 && comp(seq[j], key)) {
        seq[j + 1] = seq[j];
        for (const auto &seqVar : _multiSortColumns) {
          const auto &col = seqVar->payload.seqValue;
          col[j + 1] = col[j];
        }
        j = j - 1;
      }
      seq[j + 1] = key;
      auto z = 0;
      for (const auto &seqVar : _multiSortColumns) {
        const auto &col = seqVar->payload.seqValue;
        col[j + 1] = _multiSortKeys[z++];
      }
    }
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    JointOp::ensureJoinSetup(context, input);
    // Sort in place
    auto len = stbds_arrlen(input.payload.seqValue);
    if (!_desc) {
      insertSort(input.payload.seqValue, len, sortAsc);
    } else {
      insertSort(input.payload.seqValue, len, sortDesc);
    }
    return input;
  }
};

struct Remove : public JointOp, public BlocksUser {
  bool _fast = false;

  static inline ParamsInfo paramsInfo = ParamsInfo(
      joinOpParams,
      ParamsInfo::Param("Predicate",
                        "The blocks to use as predicate, if true the item will "
                        "be popped from the sequence.",
                        CBTypesInfo(CoreInfo::blocksInfo)),
      ParamsInfo::Param("Unordered",
                        "Turn on to remove items very quickly but will not "
                        "preserve the sequence items order.",
                        CBTypesInfo(CoreInfo::boolInfo)));

  static CBParametersInfo parameters() { return CBParametersInfo(paramsInfo); }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      return JointOp::setParam(index, value);
    case 1:
      cloneVar(_blocks, value);
      break;
    case 2:
      _fast = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return JointOp::getParam(index);
    case 1:
      return _blocks;
    case 2:
      return Var(_fast);
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }

  CBTypeInfo inferTypes(CBTypeInfo inputType, CBExposedTypesInfo consumables) {
    // need to replace input type of inner chain with inner of seq
    assert(inputType.seqType);
    BlocksUser::inferTypes(*inputType.seqType, consumables);
    return inputType;
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    JointOp::ensureJoinSetup(context, input);
    // Remove in place, will possibly remove any sorting!
    auto len = stbds_arrlen(input.payload.seqValue);
    for (auto i = len - 1; i >= 0; i--) {
      auto &var = input.payload.seqValue[i];
      CBVar output{};
      if (unlikely(!activateBlocks(_blocks.payload.seqValue, context, var,
                                   output))) {
        return StopChain;
      } else if (output == True) {
        // remove from input
        if (var.valueType >= EndOfBlittableTypes) {
          destroyVar(var);
        }
        if (_fast)
          stbds_arrdelswap(input.payload.seqValue, i);
        else
          stbds_arrdel(input.payload.seqValue, i);
        // remove from joined
        for (const auto &seqVar : _multiSortColumns) {
          const auto &seq = seqVar->payload.seqValue;
          if (seq ==
              input.payload.seqValue) // avoid removing from same seq as input!
            continue;

          auto &jvar = seq[i];
          if (var.valueType >= EndOfBlittableTypes) {
            destroyVar(jvar);
          }
          if (_fast)
            stbds_arrdelswap(seq, i);
          else
            stbds_arrdel(seq, i);
        }
      }
    }
    return input;
  }
};

struct Profile : public BlocksUser {
  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }
  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static inline ParamsInfo paramsInfo = ParamsInfo(ParamsInfo::Param(
      "Action", "The action to profile.", CBTypesInfo(CoreInfo::blocksInfo)));

  static CBParametersInfo parameters() { return CBParametersInfo(paramsInfo); }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      cloneVar(_blocks, value);
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _blocks;
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    TIMED_FUNC(timerObj);
    CBVar output{};
    if (unlikely(!activateBlocks(_blocks.payload.seqValue, context, input,
                                 output))) {
      return StopChain;
    }
    return input;
  }
};

struct XPendBase {
  static inline TypesInfo xpendTypes = TypesInfo::FromMany(
      false, CBTypeInfo(CoreInfo::anySeqInfo), CBTypeInfo(CoreInfo::bytesInfo),
      CBTypeInfo(CoreInfo::strInfo));
};

struct XpendTo : public XPendBase {
  ThreadShared<std::string> _scratchStr;

  ContextableVar _collection{};

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }
  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  // TODO use xpendTypes...
  static inline ParamsInfo paramsInfo = ParamsInfo(
      ParamsInfo::Param("Collection", "The collection to add the input to.",
                        CBTypesInfo(CoreInfo::varInfo)));

  static CBParametersInfo parameters() { return CBParametersInfo(paramsInfo); }

  CBTypeInfo inferTypes(CBTypeInfo inputType, CBExposedTypesInfo consumables) {
    auto conss = IterableExposedInfo(consumables);
    for (auto &cons : conss) {
      if (strcmp(cons.name, _collection.variableName()) == 0) {
        if (cons.exposedType.basicType != CBType::Seq &&
            cons.exposedType.basicType != CBType::Bytes &&
            cons.exposedType.basicType != CBType::String) {
          throw CBException("AppendTo/PrependTo expects either a Seq, String "
                            "or Bytes variable as collection.");
        }
        if (!cons.isMutable) {
          throw CBException(
              "AppendTo/PrependTo expects a mutable variable (Set/Push).");
        }
        if (cons.exposedType.basicType == Seq &&
            (cons.exposedType.seqType == nullptr ||
             *cons.exposedType.seqType != inputType)) {
          throw CBException("AppendTo/PrependTo input type is not compatible "
                            "with the backing Seq.");
        }
        // Validation Ok if here..
        return inputType;
      }
    }
    throw CBException("AppendTo/PrependTo: Failed to find variable: " +
                      std::string(_collection.variableName()));
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      _collection.setParam(value);
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _collection.getParam();
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }
};

struct AppendTo : public XpendTo {
  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    auto &collection = _collection.get(context);
    switch (collection.valueType) {
    case Seq: {
      stbds_arrpush(collection.payload.seqValue, input);
      break;
    }
    case String: {
      // variable is mutable, so we are sure we manage the memory
      // specifically in Set, cloneVar is used, which uses `new` to allocate
      // all we have to do use to clone our scratch on top of it
      _scratchStr().clear();
      _scratchStr() += collection.payload.stringValue;
      _scratchStr() += input.payload.stringValue;
      Var tmp(_scratchStr());
      cloneVar(collection, tmp);
      break;
    }
    default:
      break;
    }
    return input;
  }
};

struct PrependTo : public XpendTo {
  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    auto &collection = _collection.get(context);
    switch (collection.valueType) {
    case Seq: {
      stbds_arrins(collection.payload.seqValue, 0, input);
      break;
    }
    case String: {
      // variable is mutable, so we are sure we manage the memory
      // specifically in Set, cloneVar is used, which uses `new` to allocate
      // all we have to do use to clone our scratch on top of it
      _scratchStr().clear();
      _scratchStr() += input.payload.stringValue;
      _scratchStr() += collection.payload.stringValue;
      Var tmp(_scratchStr());
      cloneVar(collection, tmp);
      break;
    }
    default:
      break;
    }
    return input;
  }
};

// Register Const
RUNTIME_CORE_BLOCK_FACTORY(Const);
RUNTIME_BLOCK_destroy(Const);
RUNTIME_BLOCK_inputTypes(Const);
RUNTIME_BLOCK_outputTypes(Const);
RUNTIME_BLOCK_parameters(Const);
RUNTIME_BLOCK_inferTypes(Const);
RUNTIME_BLOCK_setParam(Const);
RUNTIME_BLOCK_getParam(Const);
RUNTIME_BLOCK_activate(Const);
RUNTIME_BLOCK_END(Const);

// Register Input
RUNTIME_CORE_BLOCK_FACTORY(Input);
RUNTIME_BLOCK_inputTypes(Input);
RUNTIME_BLOCK_outputTypes(Input);
RUNTIME_BLOCK_activate(Input);
RUNTIME_BLOCK_END(Input);

// Register Sleep
RUNTIME_CORE_BLOCK_FACTORY(Sleep);
RUNTIME_BLOCK_inputTypes(Sleep);
RUNTIME_BLOCK_outputTypes(Sleep);
RUNTIME_BLOCK_parameters(Sleep);
RUNTIME_BLOCK_setParam(Sleep);
RUNTIME_BLOCK_getParam(Sleep);
RUNTIME_BLOCK_activate(Sleep);
RUNTIME_BLOCK_END(Sleep);

// Register And
RUNTIME_CORE_BLOCK_FACTORY(And);
RUNTIME_BLOCK_inputTypes(And);
RUNTIME_BLOCK_outputTypes(And);
RUNTIME_BLOCK_activate(And);
RUNTIME_BLOCK_END(And);

// Register Or
RUNTIME_CORE_BLOCK_FACTORY(Or);
RUNTIME_BLOCK_inputTypes(Or);
RUNTIME_BLOCK_outputTypes(Or);
RUNTIME_BLOCK_activate(Or);
RUNTIME_BLOCK_END(Or);

// Register Not
RUNTIME_CORE_BLOCK_FACTORY(Not);
RUNTIME_BLOCK_inputTypes(Not);
RUNTIME_BLOCK_outputTypes(Not);
RUNTIME_BLOCK_activate(Not);
RUNTIME_BLOCK_END(Not);

// Register Stop
RUNTIME_CORE_BLOCK_FACTORY(Stop);
RUNTIME_BLOCK_inputTypes(Stop);
RUNTIME_BLOCK_outputTypes(Stop);
RUNTIME_BLOCK_activate(Stop);
RUNTIME_BLOCK_END(Stop);

// Register Restart
RUNTIME_CORE_BLOCK_FACTORY(Restart);
RUNTIME_BLOCK_inputTypes(Restart);
RUNTIME_BLOCK_outputTypes(Restart);
RUNTIME_BLOCK_activate(Restart);
RUNTIME_BLOCK_END(Restart);

// Register Return
RUNTIME_CORE_BLOCK_FACTORY(Return);
RUNTIME_BLOCK_inputTypes(Return);
RUNTIME_BLOCK_outputTypes(Return);
RUNTIME_BLOCK_activate(Return);
RUNTIME_BLOCK_END(Return);

// Register IsNan
RUNTIME_CORE_BLOCK_FACTORY(IsValidNumber);
RUNTIME_BLOCK_inputTypes(IsValidNumber);
RUNTIME_BLOCK_outputTypes(IsValidNumber);
RUNTIME_BLOCK_activate(IsValidNumber);
RUNTIME_BLOCK_END(IsValidNumber);

// Register Set
RUNTIME_CORE_BLOCK_FACTORY(Set);
RUNTIME_BLOCK_cleanup(Set);
RUNTIME_BLOCK_inputTypes(Set);
RUNTIME_BLOCK_outputTypes(Set);
RUNTIME_BLOCK_parameters(Set);
RUNTIME_BLOCK_inferTypes(Set);
RUNTIME_BLOCK_exposedVariables(Set);
RUNTIME_BLOCK_setParam(Set);
RUNTIME_BLOCK_getParam(Set);
RUNTIME_BLOCK_activate(Set);
RUNTIME_BLOCK_END(Set);

// Register Ref
RUNTIME_CORE_BLOCK_FACTORY(Ref);
RUNTIME_BLOCK_cleanup(Ref);
RUNTIME_BLOCK_inputTypes(Ref);
RUNTIME_BLOCK_outputTypes(Ref);
RUNTIME_BLOCK_parameters(Ref);
RUNTIME_BLOCK_inferTypes(Ref);
RUNTIME_BLOCK_exposedVariables(Ref);
RUNTIME_BLOCK_setParam(Ref);
RUNTIME_BLOCK_getParam(Ref);
RUNTIME_BLOCK_activate(Ref);
RUNTIME_BLOCK_END(Ref);

// Register Update
RUNTIME_CORE_BLOCK_FACTORY(Update);
RUNTIME_BLOCK_cleanup(Update);
RUNTIME_BLOCK_inputTypes(Update);
RUNTIME_BLOCK_outputTypes(Update);
RUNTIME_BLOCK_parameters(Update);
RUNTIME_BLOCK_inferTypes(Update);
RUNTIME_BLOCK_consumedVariables(Update);
RUNTIME_BLOCK_setParam(Update);
RUNTIME_BLOCK_getParam(Update);
RUNTIME_BLOCK_activate(Update);
RUNTIME_BLOCK_END(Update);

// Register Push
RUNTIME_CORE_BLOCK_FACTORY(Push);
RUNTIME_BLOCK_destroy(Push);
RUNTIME_BLOCK_cleanup(Push);
RUNTIME_BLOCK_inputTypes(Push);
RUNTIME_BLOCK_outputTypes(Push);
RUNTIME_BLOCK_parameters(Push);
RUNTIME_BLOCK_inferTypes(Push);
RUNTIME_BLOCK_exposedVariables(Push);
RUNTIME_BLOCK_setParam(Push);
RUNTIME_BLOCK_getParam(Push);
RUNTIME_BLOCK_activate(Push);
RUNTIME_BLOCK_END(Push);

// Register Pop
RUNTIME_CORE_BLOCK_FACTORY(Pop);
RUNTIME_BLOCK_cleanup(Pop);
RUNTIME_BLOCK_destroy(Pop);
RUNTIME_BLOCK_inputTypes(Pop);
RUNTIME_BLOCK_outputTypes(Pop);
RUNTIME_BLOCK_parameters(Pop);
RUNTIME_BLOCK_inferTypes(Pop);
RUNTIME_BLOCK_consumedVariables(Pop);
RUNTIME_BLOCK_setParam(Pop);
RUNTIME_BLOCK_getParam(Pop);
RUNTIME_BLOCK_activate(Pop);
RUNTIME_BLOCK_END(Pop);

// Register Count
RUNTIME_CORE_BLOCK_FACTORY(Count);
RUNTIME_BLOCK_cleanup(Count);
RUNTIME_BLOCK_inputTypes(Count);
RUNTIME_BLOCK_outputTypes(Count);
RUNTIME_BLOCK_parameters(Count);
RUNTIME_BLOCK_setParam(Count);
RUNTIME_BLOCK_getParam(Count);
RUNTIME_BLOCK_activate(Count);
RUNTIME_BLOCK_END(Count);

// Register Clear
RUNTIME_CORE_BLOCK_FACTORY(Clear);
RUNTIME_BLOCK_cleanup(Clear);
RUNTIME_BLOCK_inputTypes(Clear);
RUNTIME_BLOCK_outputTypes(Clear);
RUNTIME_BLOCK_parameters(Clear);
RUNTIME_BLOCK_setParam(Clear);
RUNTIME_BLOCK_getParam(Clear);
RUNTIME_BLOCK_activate(Clear);
RUNTIME_BLOCK_END(Clear);

// Register Get
RUNTIME_CORE_BLOCK_FACTORY(Get);
RUNTIME_BLOCK_cleanup(Get);
RUNTIME_BLOCK_destroy(Get);
RUNTIME_BLOCK_inputTypes(Get);
RUNTIME_BLOCK_outputTypes(Get);
RUNTIME_BLOCK_parameters(Get);
RUNTIME_BLOCK_inferTypes(Get);
RUNTIME_BLOCK_consumedVariables(Get);
RUNTIME_BLOCK_setParam(Get);
RUNTIME_BLOCK_getParam(Get);
RUNTIME_BLOCK_activate(Get);
RUNTIME_BLOCK_END(Get);

// Register Swap
RUNTIME_CORE_BLOCK_FACTORY(Swap);
RUNTIME_BLOCK_cleanup(Swap);
RUNTIME_BLOCK_inputTypes(Swap);
RUNTIME_BLOCK_outputTypes(Swap);
RUNTIME_BLOCK_parameters(Swap);
RUNTIME_BLOCK_consumedVariables(Swap);
RUNTIME_BLOCK_setParam(Swap);
RUNTIME_BLOCK_getParam(Swap);
RUNTIME_BLOCK_activate(Swap);
RUNTIME_BLOCK_END(Swap);

// Register Take
RUNTIME_CORE_BLOCK_FACTORY(Take);
RUNTIME_BLOCK_destroy(Take);
RUNTIME_BLOCK_cleanup(Take);
RUNTIME_BLOCK_consumedVariables(Take);
RUNTIME_BLOCK_inputTypes(Take);
RUNTIME_BLOCK_outputTypes(Take);
RUNTIME_BLOCK_parameters(Take);
RUNTIME_BLOCK_inferTypes(Take);
RUNTIME_BLOCK_setParam(Take);
RUNTIME_BLOCK_getParam(Take);
RUNTIME_BLOCK_activate(Take);
RUNTIME_BLOCK_END(Take);

// Register Limit
RUNTIME_CORE_BLOCK_FACTORY(Limit);
RUNTIME_BLOCK_destroy(Limit);
RUNTIME_BLOCK_inputTypes(Limit);
RUNTIME_BLOCK_outputTypes(Limit);
RUNTIME_BLOCK_parameters(Limit);
RUNTIME_BLOCK_inferTypes(Limit);
RUNTIME_BLOCK_setParam(Limit);
RUNTIME_BLOCK_getParam(Limit);
RUNTIME_BLOCK_activate(Limit);
RUNTIME_BLOCK_END(Limit);

// Register Repeat
RUNTIME_CORE_BLOCK_FACTORY(Repeat);
RUNTIME_BLOCK_inputTypes(Repeat);
RUNTIME_BLOCK_outputTypes(Repeat);
RUNTIME_BLOCK_parameters(Repeat);
RUNTIME_BLOCK_setParam(Repeat);
RUNTIME_BLOCK_getParam(Repeat);
RUNTIME_BLOCK_activate(Repeat);
RUNTIME_BLOCK_destroy(Repeat);
RUNTIME_BLOCK_cleanup(Repeat);
RUNTIME_BLOCK_exposedVariables(Repeat);
RUNTIME_BLOCK_consumedVariables(Repeat);
RUNTIME_BLOCK_inferTypes(Repeat);
RUNTIME_BLOCK_END(Repeat);

// Register Sort
RUNTIME_CORE_BLOCK(Sort);
RUNTIME_BLOCK_inputTypes(Sort);
RUNTIME_BLOCK_outputTypes(Sort);
RUNTIME_BLOCK_activate(Sort);
RUNTIME_BLOCK_parameters(Sort);
RUNTIME_BLOCK_setParam(Sort);
RUNTIME_BLOCK_getParam(Sort);
RUNTIME_BLOCK_cleanup(Sort);
RUNTIME_BLOCK_END(Sort);

// Register
RUNTIME_CORE_BLOCK(Remove);
RUNTIME_BLOCK_inputTypes(Remove);
RUNTIME_BLOCK_outputTypes(Remove);
RUNTIME_BLOCK_parameters(Remove);
RUNTIME_BLOCK_setParam(Remove);
RUNTIME_BLOCK_getParam(Remove);
RUNTIME_BLOCK_activate(Remove);
RUNTIME_BLOCK_destroy(Remove);
RUNTIME_BLOCK_cleanup(Repeat);
RUNTIME_BLOCK_inferTypes(Remove);
RUNTIME_BLOCK_END(Remove);

// Register
RUNTIME_CORE_BLOCK(Profile);
RUNTIME_BLOCK_inputTypes(Profile);
RUNTIME_BLOCK_outputTypes(Profile);
RUNTIME_BLOCK_parameters(Profile);
RUNTIME_BLOCK_setParam(Profile);
RUNTIME_BLOCK_getParam(Profile);
RUNTIME_BLOCK_activate(Profile);
RUNTIME_BLOCK_destroy(Profile);
RUNTIME_BLOCK_cleanup(Profile);
RUNTIME_BLOCK_inferTypes(Profile);
RUNTIME_BLOCK_END(Profile);

// Register PrependTo
RUNTIME_CORE_BLOCK(PrependTo);
RUNTIME_BLOCK_inputTypes(PrependTo);
RUNTIME_BLOCK_outputTypes(PrependTo);
RUNTIME_BLOCK_parameters(PrependTo);
RUNTIME_BLOCK_inferTypes(PrependTo);
RUNTIME_BLOCK_setParam(PrependTo);
RUNTIME_BLOCK_getParam(PrependTo);
RUNTIME_BLOCK_activate(PrependTo);
RUNTIME_BLOCK_END(PrependTo);

// Register PrependTo
RUNTIME_CORE_BLOCK(AppendTo);
RUNTIME_BLOCK_inputTypes(AppendTo);
RUNTIME_BLOCK_outputTypes(AppendTo);
RUNTIME_BLOCK_parameters(AppendTo);
RUNTIME_BLOCK_inferTypes(AppendTo);
RUNTIME_BLOCK_setParam(AppendTo);
RUNTIME_BLOCK_getParam(AppendTo);
RUNTIME_BLOCK_activate(AppendTo);
RUNTIME_BLOCK_END(AppendTo);

LOGIC_OP_DESC(Is);
LOGIC_OP_DESC(IsNot);
LOGIC_OP_DESC(IsMore);
LOGIC_OP_DESC(IsLess);
LOGIC_OP_DESC(IsMoreEqual);
LOGIC_OP_DESC(IsLessEqual);

LOGIC_OP_DESC(Any);
LOGIC_OP_DESC(All);
LOGIC_OP_DESC(AnyNot);
LOGIC_OP_DESC(AllNot);
LOGIC_OP_DESC(AnyMore);
LOGIC_OP_DESC(AllMore);
LOGIC_OP_DESC(AnyLess);
LOGIC_OP_DESC(AllLess);
LOGIC_OP_DESC(AnyMoreEqual);
LOGIC_OP_DESC(AllMoreEqual);
LOGIC_OP_DESC(AnyLessEqual);
LOGIC_OP_DESC(AllLessEqual);

namespace Math {
MATH_BINARY_BLOCK(Add);
MATH_BINARY_BLOCK(Subtract);
MATH_BINARY_BLOCK(Multiply);
MATH_BINARY_BLOCK(Divide);
MATH_BINARY_BLOCK(Xor);
MATH_BINARY_BLOCK(And);
MATH_BINARY_BLOCK(Or);
MATH_BINARY_BLOCK(Mod);
MATH_BINARY_BLOCK(LShift);
MATH_BINARY_BLOCK(RShift);

MATH_UNARY_BLOCK(Abs);
MATH_UNARY_BLOCK(Exp);
MATH_UNARY_BLOCK(Exp2);
MATH_UNARY_BLOCK(Expm1);
MATH_UNARY_BLOCK(Log);
MATH_UNARY_BLOCK(Log10);
MATH_UNARY_BLOCK(Log2);
MATH_UNARY_BLOCK(Log1p);
MATH_UNARY_BLOCK(Sqrt);
MATH_UNARY_BLOCK(Cbrt);
MATH_UNARY_BLOCK(Sin);
MATH_UNARY_BLOCK(Cos);
MATH_UNARY_BLOCK(Tan);
MATH_UNARY_BLOCK(Asin);
MATH_UNARY_BLOCK(Acos);
MATH_UNARY_BLOCK(Atan);
MATH_UNARY_BLOCK(Sinh);
MATH_UNARY_BLOCK(Cosh);
MATH_UNARY_BLOCK(Tanh);
MATH_UNARY_BLOCK(Asinh);
MATH_UNARY_BLOCK(Acosh);
MATH_UNARY_BLOCK(Atanh);
MATH_UNARY_BLOCK(Erf);
MATH_UNARY_BLOCK(Erfc);
MATH_UNARY_BLOCK(TGamma);
MATH_UNARY_BLOCK(LGamma);
MATH_UNARY_BLOCK(Ceil);
MATH_UNARY_BLOCK(Floor);
MATH_UNARY_BLOCK(Trunc);
MATH_UNARY_BLOCK(Round);

RUNTIME_BLOCK(Math, Mean);
RUNTIME_BLOCK_inputTypes(Mean);
RUNTIME_BLOCK_outputTypes(Mean);
RUNTIME_BLOCK_activate(Mean);
RUNTIME_BLOCK_END(Mean);
}; // namespace Math

void registerBlocksCoreBlocks() {
  REGISTER_CORE_BLOCK(Const);
  REGISTER_CORE_BLOCK(Input);
  REGISTER_CORE_BLOCK(Set);
  REGISTER_CORE_BLOCK(Ref);
  REGISTER_CORE_BLOCK(Update);
  REGISTER_CORE_BLOCK(Push);
  REGISTER_CORE_BLOCK(Pop);
  REGISTER_CORE_BLOCK(Clear);
  REGISTER_CORE_BLOCK(Count);
  REGISTER_CORE_BLOCK(Get);
  REGISTER_CORE_BLOCK(Swap);
  REGISTER_CORE_BLOCK(Sleep);
  REGISTER_CORE_BLOCK(Restart);
  REGISTER_CORE_BLOCK(Return);
  REGISTER_CORE_BLOCK(Stop);
  REGISTER_CORE_BLOCK(And);
  REGISTER_CORE_BLOCK(Or);
  REGISTER_CORE_BLOCK(Not);
  REGISTER_CORE_BLOCK(IsValidNumber);
  REGISTER_CORE_BLOCK(Take);
  REGISTER_CORE_BLOCK(Limit);
  REGISTER_CORE_BLOCK(Repeat);
  REGISTER_CORE_BLOCK(Sort);
  REGISTER_CORE_BLOCK(Remove);
  REGISTER_CORE_BLOCK(Profile);
  REGISTER_CORE_BLOCK(PrependTo);
  REGISTER_CORE_BLOCK(AppendTo);
  REGISTER_CORE_BLOCK(Is);
  REGISTER_CORE_BLOCK(IsNot);
  REGISTER_CORE_BLOCK(IsMore);
  REGISTER_CORE_BLOCK(IsLess);
  REGISTER_CORE_BLOCK(IsMoreEqual);
  REGISTER_CORE_BLOCK(IsLessEqual);
  REGISTER_CORE_BLOCK(Any);
  REGISTER_CORE_BLOCK(All);
  REGISTER_CORE_BLOCK(AnyNot);
  REGISTER_CORE_BLOCK(AllNot);
  REGISTER_CORE_BLOCK(AnyMore);
  REGISTER_CORE_BLOCK(AllMore);
  REGISTER_CORE_BLOCK(AnyLess);
  REGISTER_CORE_BLOCK(AllLess);
  REGISTER_CORE_BLOCK(AnyMoreEqual);
  REGISTER_CORE_BLOCK(AllMoreEqual);
  REGISTER_CORE_BLOCK(AnyLessEqual);
  REGISTER_CORE_BLOCK(AllLessEqual);

  REGISTER_BLOCK(Math, Add);
  REGISTER_BLOCK(Math, Subtract);
  REGISTER_BLOCK(Math, Multiply);
  REGISTER_BLOCK(Math, Divide);
  REGISTER_BLOCK(Math, Xor);
  REGISTER_BLOCK(Math, And);
  REGISTER_BLOCK(Math, Or);
  REGISTER_BLOCK(Math, Mod);
  REGISTER_BLOCK(Math, LShift);
  REGISTER_BLOCK(Math, RShift);

  REGISTER_BLOCK(Math, Abs);
  REGISTER_BLOCK(Math, Exp);
  REGISTER_BLOCK(Math, Exp2);
  REGISTER_BLOCK(Math, Expm1);
  REGISTER_BLOCK(Math, Log);
  REGISTER_BLOCK(Math, Log10);
  REGISTER_BLOCK(Math, Log2);
  REGISTER_BLOCK(Math, Log1p);
  REGISTER_BLOCK(Math, Sqrt);
  REGISTER_BLOCK(Math, Cbrt);
  REGISTER_BLOCK(Math, Sin);
  REGISTER_BLOCK(Math, Cos);
  REGISTER_BLOCK(Math, Tan);
  REGISTER_BLOCK(Math, Asin);
  REGISTER_BLOCK(Math, Acos);
  REGISTER_BLOCK(Math, Atan);
  REGISTER_BLOCK(Math, Sinh);
  REGISTER_BLOCK(Math, Cosh);
  REGISTER_BLOCK(Math, Tanh);
  REGISTER_BLOCK(Math, Asinh);
  REGISTER_BLOCK(Math, Acosh);
  REGISTER_BLOCK(Math, Atanh);
  REGISTER_BLOCK(Math, Erf);
  REGISTER_BLOCK(Math, Erfc);
  REGISTER_BLOCK(Math, TGamma);
  REGISTER_BLOCK(Math, LGamma);
  REGISTER_BLOCK(Math, Ceil);
  REGISTER_BLOCK(Math, Floor);
  REGISTER_BLOCK(Math, Trunc);
  REGISTER_BLOCK(Math, Round);

  REGISTER_BLOCK(Math, Mean);
}
}; // namespace chainblockseys); y++) {
            auto &key = tableKeys[y];
            if (_key == key) {
              return tableTypes[y];
            }
          }
        }
      }
      if (_defaultValue.valueType != None) {
        freeDerivedInfo(_defaultType);
        _defaultType = deriveTypeInfo(_defaultValue);
        return _defaultType;
      } else {
        throw CBException(
            "Get: Could not infer an output type, key not found.");
      }
    } else {
      for (auto i = 0; i < stbds_arrlen(consumableVariables); i++) {
        auto &cv = consumableVariables[i];
        if (_name == cv.name) {
          return cv.exposedType;
        }
      }
    }
    if (_defaultValue.valueType != None) {
      freeDerivedInfo(_defaultType);
      _defaultType = deriveTypeInfo(_defaultValue);
      return _defaultType;
    } else {
      throw CBException("Get: Could not infer an output type.");
    }
  }

  CBExposedTypesInfo consumedVariables() {
    if (_defaultValue.valueType != None) {
      return nullptr;
    } else {
      if (_isTable) {
        _exposedInfo = ExposedInfo(
            ExposedInfo::Variable(_name.c_str(), "The consumed table.",
                                  CBTypeInfo(CoreInfo::tableInfo)));
      } else {
        _exposedInfo = ExposedInfo(
            ExposedInfo::Variable(_name.c_str(), "The consumed variable.",
                                  CBTypeInfo(CoreInfo::anyInfo)));
      }
      return CBExposedTypesInfo(_exposedInfo);
    }
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (_shortCut)
      return *_target;

    if (!_target) {
      _target = contextVariable(context, _name.c_str(), _global);
    }
    if (_isTable) {
      if (_target->valueType == Table) {
        ptrdiff_t index =
            stbds_shgeti(_target->payload.tableValue, _key.c_str());
        if (index == -1) {
          if (_defaultType.basicType != None) {
            return _defaultValue;
          } else {
            return Var::Restart();
          }
        }
        auto &value = _target->payload.tableValue[index].value;
        if (unlikely(_defaultValue.valueType != None &&
                     value.valueType != _defaultValue.valueType)) {
          return _defaultValue;
        } else {
          return value;
        }
      } else {
        if (_defaultType.basicType != None) {
          return _defaultValue;
        } else {
          return Var::Restart();
        }
      }
    } else {
      auto &value = *_target;
      if (unlikely(_defaultValue.valueType != None &&
                   value.valueType != _defaultValue.valueType)) {
        return _defaultValue;
      } else {
        // Fastest path, flag it as shortcut
        _shortCut = true;
        return value;
      }
    }
  }
};

struct Swap {
  static inline ParamsInfo swapParamsInfo =
      ParamsInfo(ParamsInfo::Param("NameA", "The name of first variable.",
                                   CBTypesInfo(CoreInfo::strInfo)),
                 ParamsInfo::Param("NameB", "The name of second variable.",
                                   CBTypesInfo(CoreInfo::strInfo)));

  std::string _nameA;
  std::string _nameB;
  CBVar *_targetA{};
  CBVar *_targetB{};
  ExposedInfo _exposedInfo;

  void cleanup() {
    _targetA = nullptr;
    _targetB = nullptr;
  }

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  CBExposedTypesInfo consumedVariables() {
    _exposedInfo = ExposedInfo(
        ExposedInfo::Variable(_nameA.c_str(), "The consumed variable.",
                              CBTypeInfo(CoreInfo::anyInfo)),
        ExposedInfo::Variable(_nameB.c_str(), "The consumed variable.",
                              CBTypeInfo(CoreInfo::anyInfo)));
    return CBExposedTypesInfo(_exposedInfo);
  }

  static CBParametersInfo parameters() {
    return CBParametersInfo(swapParamsInfo);
  }

  void setParam(int index, CBVar value) {
    if (index == 0)
      _nameA = value.payload.stringValue;
    else if (index == 1) {
      _nameB = value.payload.stringValue;
    }
  }

  CBVar getParam(int index) {
    if (index == 0)
      return Var(_nameA.c_str());
    else if (index == 1)
      return Var(_nameB.c_str());
    throw CBException("Param index out of range.");
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (!_targetA) {
      _targetA = contextVariable(context, _nameA.c_str());
      _targetB = contextVariable(context, _nameB.c_str());
    }
    auto tmp = *_targetA;
    *_targetA = *_targetB;
    *_targetB = tmp;
    return input;
  }
};

struct Push : public VariableBase {
  bool _clear = true;
  bool _firstPusher = false; // if we are the initializers!
  bool _tableOwner = false;  // we are the first in the table too!
  CBTypeInfo _seqInfo{};
  CBTypeInfo _seqInnerInfo{};
  CBTypeInfo _tableInfo{};

  static inline ParamsInfo pushParams = ParamsInfo(
      variableParamsInfo,
      ParamsInfo::Param(
          "Clear",
          "If we should clear this sequence at every chain iteration; works "
          "only if this is the first push; default: true.",
          CBTypesInfo(CoreInfo::boolInfo)));

  static CBParametersInfo parameters() { return CBParametersInfo(pushParams); }

  void setParam(int index, CBVar value) {
    if (index <= 2)
      VariableBase::setParam(index, value);
    else if (index == 3) {
      _clear = value.payload.boolValue;
    }
  }

  CBVar getParam(int index) {
    if (index <= 2)
      return VariableBase::getParam(index);
    else if (index == 3)
      return Var(_clear);
    throw CBException("Param index out of range.");
  }

  void destroy() {
    if (_firstPusher) {
      if (_tableInfo.tableKeys)
        stbds_arrfree(_tableInfo.tableKeys);
      if (_tableInfo.tableTypes)
        stbds_arrfree(_tableInfo.tableTypes);
    }
  }

  CBTypeInfo inferTypes(CBTypeInfo inputType,
                        CBExposedTypesInfo consumableVariables) {
    if (_isTable) {
      auto tableFound = false;
      for (auto i = 0; stbds_arrlen(consumableVariables) > i; i++) {
        if (consumableVariables[i].name == _name &&
            consumableVariables[i].exposedType.tableTypes) {
          auto &tableKeys = consumableVariables[i].exposedType.tableKeys;
          auto &tableTypes = consumableVariables[i].exposedType.tableTypes;
          tableFound = true;
          for (auto y = 0; y < stbds_arrlen(tableKeys); y++) {
            if (_key == tableKeys[y] && tableTypes[y].basicType == Seq) {
              return inputType; // found lets escape
            }
          }
        }
      }
      if (!tableFound) {
        // Assume we are the first pushing
        _tableOwner = true;
      }
      _firstPusher = true;
      _tableInfo.basicType = Table;
      if (_tableInfo.tableTypes) {
        stbds_arrfree(_tableInfo.tableTypes);
      }
      if (_tableInfo.tableKeys) {
        stbds_arrfree(_tableInfo.tableKeys);
      }
      _seqInfo.basicType = Seq;
      _seqInnerInfo = inputType;
      _seqInfo.seqType = &_seqInnerInfo;
      stbds_arrpush(_tableInfo.tableTypes, _seqInfo);
      stbds_arrpush(_tableInfo.tableKeys, _key.c_str());
      _exposedInfo = ExposedInfo(ExposedInfo::Variable(
          _name.c_str(), "The exposed table.", CBTypeInfo(_tableInfo), true));
    } else {
      for (auto i = 0; i < stbds_arrlen(consumableVariables); i++) {
        auto &cv = consumableVariables[i];
        if (_name == cv.name && cv.exposedType.basicType == Seq) {
          return inputType; // found lets escape
        }
      }
      // Assume we are the first pushing this variable
      _firstPusher = true;
      _seqInfo.basicType = Seq;
      _seqInnerInfo = inputType;
      _seqInfo.seqType = &_seqInnerInfo;
      _exposedInfo = ExposedInfo(ExposedInfo::Variable(
          _name.c_str(), "The exposed sequence.", _seqInfo, true));
    }
    return inputType;
  }

  CBExposedTypesInfo exposedVariables() {
    if (_firstPusher) {
      return CBExposedTypesInfo(_exposedInfo);
    } else {
      return nullptr;
    }
  }

  void cleanup() {
    if (_firstPusher && _target) {
      if (_isTable && _target->valueType == Table) {
        ptrdiff_t index =
            stbds_shgeti(_target->payload.tableValue, _key.c_str());
        if (index != -1) {
          auto &seq = _target->payload.tableValue[index].value;
          if (seq.valueType == Seq) {
            for (auto i = 0; i < stbds_arrlen(seq.payload.seqValue); i++) {
              destroyVar(seq.payload.seqValue[i]);
            }
          }
          stbds_shdel(_target->payload.tableValue, _key.c_str());
        }
        if (_tableOwner && stbds_shlen(_target->payload.tableValue) == 0) {
          stbds_shfree(_target->payload.tableValue);
          memset(_target, 0x0, sizeof(CBVar));
        }
      } else if (_target->valueType == Seq) {
        for (auto i = 0; i < stbds_arrlen(_target->payload.seqValue); i++) {
          destroyVar(_target->payload.seqValue[i]);
        }
        stbds_arrfree(_target->payload.seqValue);
      }
    }
    _target = nullptr;
  }

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (!_target) {
      _target = contextVariable(context, _name.c_str(), _global);
    }
    if (_isTable) {
      if (_target->valueType != Table) {
        // Not initialized yet
        _target->valueType = Table;
        _target->payload.tableValue = nullptr;
      }

      ptrdiff_t index = stbds_shgeti(_target->payload.tableValue, _key.c_str());
      if (index == -1) {
        // First empty insertion
        CBVar tmp{};
        stbds_shput(_target->payload.tableValue, _key.c_str(), tmp);
        index = stbds_shgeti(_target->payload.tableValue, _key.c_str());
      }

      auto &seq = _target->payload.tableValue[index].value;

      if (seq.valueType != Seq) {
        seq.valueType = Seq;
        seq.payload.seqValue = nullptr;
      }

      if (_firstPusher && _clear) {
        auto len = stbds_arrlen(seq.payload.seqValue);
        if (len > 0) {
          if (seq.payload.seqValue[0].valueType >= EndOfBlittableTypes) {
            // Clean allocation garbage in case it's not blittable!
            for (auto i = 0; i < len; i++) {
              destroyVar(seq.payload.seqValue[i]);
            }
          }
          stbds_arrsetlen(seq.payload.seqValue, 0);
        }
      }

      CBVar tmp{};
      cloneVar(tmp, input);
      stbds_arrpush(seq.payload.seqValue, tmp);
    } else {
      if (_target->valueType != Seq) {
        _target->valueType = Seq;
        _target->payload.seqValue = nullptr;
      }

      if (_firstPusher && _clear) {
        auto len = stbds_arrlen(_target->payload.seqValue);
        if (len > 0) {
          if (_target->payload.seqValue[0].valueType >= EndOfBlittableTypes) {
            // Clean allocation garbage in case it's not blittable!
            for (auto i = 0; i < len; i++) {
              destroyVar(_target->payload.seqValue[i]);
            }
          }
          stbds_arrsetlen(_target->payload.seqValue, 0);
        }
      }

      CBVar tmp{};
      cloneVar(tmp, input);
      stbds_arrpush(_target->payload.seqValue, tmp);
    }
    return input;
  }
};

struct SeqUser : VariableBase {
  bool _blittable = false;

  void cleanup() { _target = nullptr; }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  CBExposedTypesInfo consumedVariables() {
    if (_isTable) {
      _exposedInfo = ExposedInfo(
          ExposedInfo::Variable(_name.c_str(), "The consumed table.",
                                CBTypeInfo(CoreInfo::tableInfo)));
    } else {
      _exposedInfo = ExposedInfo(
          ExposedInfo::Variable(_name.c_str(), "The consumed variable.",
                                CBTypeInfo(CoreInfo::anyInfo)));
    }
    return CBExposedTypesInfo(_exposedInfo);
  }
};

struct Count : SeqUser {
  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::noneInfo); }
  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::intInfo); }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (!_target) {
      _target = contextVariable(context, _name.c_str(), _global);
    }
    if (_isTable) {
      if (_target->valueType != Table) {
        throw CBException("Variable is not a table, failed to Count.");
      }

      ptrdiff_t index = stbds_shgeti(_target->payload.tableValue, _key.c_str());
      if (index == -1) {
        return Var(0);
      }

      auto &seq = _target->payload.tableValue[index].value;
      if (seq.valueType != Seq) {
        return Var(0);
      }

      return Var(int64_t(stbds_arrlen(seq.payload.seqValue)));
    } else {
      if (_target->valueType != Seq) {
        return Var(0);
      }

      return Var(int64_t(stbds_arrlen(_target->payload.seqValue)));
    }
  }
};

struct Clear : SeqUser {
  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (!_target) {
      _target = contextVariable(context, _name.c_str(), _global);
    }
    if (_isTable) {
      if (_target->valueType != Table) {
        throw CBException("Variable is not a table, failed to Clear.");
      }

      ptrdiff_t index = stbds_shgeti(_target->payload.tableValue, _key.c_str());
      if (index == -1) {
        return input;
      }

      auto &seq = _target->payload.tableValue[index].value;
      if (seq.valueType != Seq) {
        return input;
      }

      auto len = stbds_arrlen(seq.payload.seqValue);
      if (len > 0) {
        if (seq.payload.seqValue[0].valueType >= EndOfBlittableTypes) {
          // Clean allocation garbage in case it's not blittable!
          for (auto i = 0; i < len; i++) {
            destroyVar(seq.payload.seqValue[i]);
          }
        }
        stbds_arrsetlen(seq.payload.seqValue, 0);
      }
    } else {
      if (_target->valueType != Seq) {
        return input;
      }

      auto len = stbds_arrlen(_target->payload.seqValue);
      if (len > 0) {
        if (_target->payload.seqValue[0].valueType >= EndOfBlittableTypes) {
          // Clean allocation garbage in case it's not blittable!
          for (auto i = 0; i < len; i++) {
            destroyVar(_target->payload.seqValue[i]);
          }
        }
        stbds_arrsetlen(_target->payload.seqValue, 0);
      }
    }
    return input;
  }
};

struct Pop : SeqUser {
  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::noneInfo); }

  CBVar _output{};

  void destroy() { destroyVar(_output); }

  CBTypeInfo inferTypes(CBTypeInfo inputType,
                        CBExposedTypesInfo consumableVariables) {
    if (_isTable) {
      for (auto i = 0; stbds_arrlen(consumableVariables) > i; i++) {
        if (consumableVariables[i].name == _name &&
            consumableVariables[i].exposedType.tableTypes) {
          auto &tableKeys = consumableVariables[i].exposedType.tableKeys;
          auto &tableTypes = consumableVariables[i].exposedType.tableTypes;
          for (auto y = 0; y < stbds_arrlen(tableKeys); y++) {
            if (_key == tableKeys[y] && tableTypes[y].basicType == Seq) {
              if (tableTypes[y].seqType != nullptr &&
                  tableTypes[y].seqType->basicType < EndOfBlittableTypes) {
                _blittable = true;
              } else {
                _blittable = false;
              }
              return *tableTypes[y].seqType; // found lets escape
            }
          }
        }
      }
      throw CBException("Pop: key not found or key value is not a sequence!.");
    } else {
      for (auto i = 0; i < stbds_arrlen(consumableVariables); i++) {
        auto &cv = consumableVariables[i];
        if (_name == cv.name && cv.exposedType.basicType == Seq) {
          if (cv.exposedType.seqType != nullptr &&
              cv.exposedType.seqType->basicType < EndOfBlittableTypes) {
            _blittable = true;
          } else {
            _blittable = false;
          }
          return *cv.exposedType.seqType; // found lets escape
        }
      }
    }
    throw CBException("Variable is not a sequence.");
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    if (!_target) {
      _target = contextVariable(context, _name.c_str(), _global);
    }
    if (_isTable) {
      if (_target->valueType != Table) {
        throw CBException("Variable (in table) is not a table, failed to Pop.");
      }

      ptrdiff_t index = stbds_shgeti(_target->payload.tableValue, _key.c_str());
      if (index == -1) {
        throw CBException("Record not found in table, failed to Pop.");
      }

      auto &seq = _target->payload.tableValue[index].value;
      if (seq.valueType != Seq) {
        throw CBException(
            "Variable (in table) is not a sequence, failed to Pop.");
      }

      if (stbds_arrlen(seq.payload.seqValue) == 0) {
        throw CBException("Pop: sequence was empty.");
      }

      // Clone and make the var ours, also if not blittable clear actual
      // sequence garbage
      auto pops = stbds_arrpop(seq.payload.seqValue);
      cloneVar(_output, pops);
      if (!_blittable) {
        destroyVar(pops);
      }
      return _output;
    } else {
      if (_target->valueType != Seq) {
        throw CBException("Variable is not a sequence, failed to Pop.");
      }

      if (stbds_arrlen(_target->payload.seqValue) == 0) {
        throw CBException("Pop: sequence was empty.");
      }

      // Clone and make the var ours, also if not blittable clear actual
      // sequence garbage
      auto pops = stbds_arrpop(_target->payload.seqValue);
      cloneVar(_output, pops);
      if (!_blittable) {
        destroyVar(pops);
      }
      return _output;
    }
  }
};

struct Take {
  static inline ParamsInfo indicesParamsInfo = ParamsInfo(ParamsInfo::Param(
      "Indices", "One or multiple indices to filter from a sequence.",
      CBTypesInfo(CoreInfo::intsVarInfo)));

  CBSeq _cachedResult = nullptr;
  CBVar _indices{};
  CBVar *_indicesVar = nullptr;
  ExposedInfo _exposedInfo{};

  void destroy() {
    if (_cachedResult) {
      stbds_arrfree(_cachedResult);
    }
    destroyVar(_indices);
  }

  void cleanup() { _indicesVar = nullptr; }

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anySeqInfo); }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBParametersInfo parameters() {
    return CBParametersInfo(indicesParamsInfo);
  }

  CBTypeInfo inferTypes(CBTypeInfo inputType,
                        CBExposedTypesInfo consumableVariables) {
    // Figure if we output a sequence or not
    if (_indices.valueType == Seq) {
      if (inputType.basicType == Seq) {
        return inputType; // multiple values
      }
    } else if (_indices.valueType == Int) {
      if (inputType.basicType == Seq && inputType.seqType) {
        return *inputType.seqType; // single value
      }
    } else { // ContextVar
      IterableExposedInfo infos(consumableVariables);
      for (auto &info : infos) {
        if (strcmp(info.name, _indices.payload.stringValue) == 0) {
          if (info.exposedType.basicType == Seq && info.exposedType.seqType &&
              info.exposedType.seqType->basicType == Int) {
            if (inputType.basicType == Seq) {
              return inputType; // multiple values
            }
          } else if (info.exposedType.basicType == Int) {
            if (inputType.basicType == Seq && inputType.seqType) {
              return *inputType.seqType; // single value
            }
          } else {
            auto msg = "Take indices variable " + std::string(info.name) +
                       " expected to be either a Seq or a Int";
            throw CBException(msg);
          }
        }
      }
    }
    throw CBException("Take expected a sequence as input.");
  }

  CBExposedTypesInfo consumedVariables() {
    if (_indices.valueType == ContextVar) {
      _exposedInfo =
          ExposedInfo(ExposedInfo::Variable(_indices.payload.stringValue,
                                            "The consumed variable.",
                                            CBTypeInfo(CoreInfo::intInfo)),
                      ExposedInfo::Variable(_indices.payload.stringValue,
                                            "The consumed variables.",
                                            CBTypeInfo(CoreInfo::intSeqInfo)));
      return CBExposedTypesInfo(_exposedInfo);
    } else {
      return nullptr;
    }
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      cloneVar(_indices, value);
      _indicesVar = nullptr;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) { return _indices; }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    auto inputLen = stbds_arrlen(input.payload.seqValue);
    auto indices = _indices;

    if (_indices.valueType == ContextVar && !_indicesVar) {
      _indicesVar = contextVariable(context, _indices.payload.stringValue);
    }

    if (_indicesVar) {
      indices = *_indicesVar;
    }

    if (indices.valueType == Int) {
      auto index = indices.payload.intValue;
      if (index >= inputLen) {
        LOG(ERROR) << "Take out of range! len:" << inputLen
                   << " wanted index: " << index;
        throw CBException("Take out of range!");
      }
      return input.payload.seqValue[index];
    } else {
      // Else it's a seq
      auto nindices = stbds_arrlen(indices.payload.seqValue);
      stbds_arrsetlen(_cachedResult, nindices);
      for (auto i = 0; nindices > i; i++) {
        auto index = indices.payload.seqValue[i].payload.intValue;
        if (index >= inputLen) {
          LOG(ERROR) << "Take out of range! len:" << inputLen
                     << " wanted index: " << index;
          throw CBException("Take out of range!");
        }
        _cachedResult[i] = input.payload.seqValue[index];
      }
      return Var(_cachedResult);
    }
  }
};

struct Limit {
  static inline ParamsInfo indicesParamsInfo = ParamsInfo(ParamsInfo::Param(
      "Max", "How many maximum elements to take from the input sequence.",
      CBTypesInfo(CoreInfo::intInfo)));

  CBSeq _cachedResult = nullptr;
  int64_t _max = 0;

  void destroy() {
    if (_cachedResult) {
      stbds_arrfree(_cachedResult);
    }
  }

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anySeqInfo); }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBParametersInfo parameters() {
    return CBParametersInfo(indicesParamsInfo);
  }

  CBTypeInfo inferTypes(CBTypeInfo inputType,
                        CBExposedTypesInfo consumableVariables) {
    // Figure if we output a sequence or not
    if (_max > 1) {
      if (inputType.basicType == Seq) {
        return inputType; // multiple values
      }
    } else {
      if (inputType.basicType == Seq && inputType.seqType) {
        return *inputType.seqType; // single value
      }
    }
    throw CBException("Limit expected a sequence as input.");
  }

  void setParam(int index, CBVar value) { _max = value.payload.intValue; }

  CBVar getParam(int index) { return Var(_max); }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    int64_t inputLen = stbds_arrlen(input.payload.seqValue);

    if (_max == 1) {
      auto index = 0;
      if (index >= inputLen) {
        LOG(ERROR) << "Limit out of range! len:" << inputLen
                   << " wanted index: " << index;
        throw CBException("Limit out of range!");
      }
      return input.payload.seqValue[index];
    } else {
      // Else it's a seq
      auto nindices = std::min(inputLen, _max);
      stbds_arrsetlen(_cachedResult, nindices);
      for (auto i = 0; i < nindices; i++) {
        _cachedResult[i] = input.payload.seqValue[i];
      }
      return Var(_cachedResult);
    }
  }
};

struct BlocksUser {
  CBVar _blocks{};
  CBValidationResult _chainValidation{};

  void destroy() {
    if (_blocks.valueType == Seq) {
      for (auto i = 0; i < stbds_arrlen(_blocks.payload.seqValue); i++) {
        auto &blk = _blocks.payload.seqValue[i].payload.blockValue;
        blk->cleanup(blk);
        blk->destroy(blk);
      }
    } else if (_blocks.valueType == Block) {
      _blocks.payload.blockValue->cleanup(_blocks.payload.blockValue);
      _blocks.payload.blockValue->destroy(_blocks.payload.blockValue);
    }
    destroyVar(_blocks);
    stbds_arrfree(_chainValidation.exposedInfo);
  }

  void cleanup() {
    if (_blocks.valueType == Seq) {
      for (auto i = 0; i < stbds_arrlen(_blocks.payload.seqValue); i++) {
        auto &blk = _blocks.payload.seqValue[i].payload.blockValue;
        blk->cleanup(blk);
      }
    } else if (_blocks.valueType == Block) {
      _blocks.payload.blockValue->cleanup(_blocks.payload.blockValue);
    }
  }

  CBTypeInfo inferTypes(CBTypeInfo inputType, CBExposedTypesInfo consumables) {
    // Free any previous result!
    stbds_arrfree(_chainValidation.exposedInfo);
    _chainValidation.exposedInfo = nullptr;

    std::vector<CBlock *> blocks;
    if (_blocks.valueType == Block) {
      blocks.push_back(_blocks.payload.blockValue);
    } else {
      for (auto i = 0; i < stbds_arrlen(_blocks.payload.seqValue); i++) {
        blocks.push_back(_blocks.payload.seqValue[i].payload.blockValue);
      }
    }
    _chainValidation = validateConnections(
        blocks,
        [](const CBlock *errorBlock, const char *errorTxt, bool nonfatalWarning,
           void *userData) {
          if (!nonfatalWarning) {
            LOG(ERROR) << "Failed inner chain validation, error: " << errorTxt;
            throw CBException("Failed inner chain validation.");
          } else {
            LOG(INFO) << "Warning during inner chain validation: " << errorTxt;
          }
        },
        this, inputType, consumables);

    return inputType;
  }

  CBExposedTypesInfo exposedVariables() { return _chainValidation.exposedInfo; }
};

struct Repeat : public BlocksUser {
  std::string _ctxVar;
  CBVar *_ctxTimes = nullptr;
  int64_t _times = 0;
  bool _forever = false;
  ExposedInfo _consumedInfo{};

  void cleanup() {
    BlocksUser::cleanup();
    _ctxTimes = nullptr;
  }

  static inline ParamsInfo repeatParamsInfo = ParamsInfo(
      ParamsInfo::Param("Action", "The blocks to repeat.",
                        CBTypesInfo(CoreInfo::blocksInfo)),
      ParamsInfo::Param("Times", "How many times we should repeat the action.",
                        CBTypesInfo(CoreInfo::intVarInfo)),
      ParamsInfo::Param("Forever", "If we should repeat the action forever.",
                        CBTypesInfo(CoreInfo::boolInfo)));

  static CBTypesInfo inputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBTypesInfo outputTypes() { return CBTypesInfo(CoreInfo::anyInfo); }

  static CBParametersInfo parameters() {
    return CBParametersInfo(repeatParamsInfo);
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      cloneVar(_blocks, value);
      break;
    case 1:
      if (value.valueType == Int) {
        _ctxVar.clear();
        _times = value.payload.intValue;
      } else {
        _ctxVar.assign(value.payload.stringValue);
        _ctxTimes = nullptr;
      }
      break;
    case 2:
      _forever = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return _blocks;
    case 1:
      if (_ctxVar.size() == 0) {
        return Var(_times);
      } else {
        auto ctxTimes = Var(_ctxVar);
        ctxTimes.valueType = ContextVar;
        return ctxTimes;
      }
    case 2:
      return Var(_forever);
    default:
      break;
    }
    throw CBException("Parameter out of range.");
  }

  CBExposedTypesInfo consumedVariables() {
    if (_ctxVar.size() == 0) {
      return nullptr;
    } else {
      _consumedInfo = ExposedInfo(ExposedInfo::Variable(
          _ctxVar.c_str(), "The Int number of repeats variable.",
          CBTypeInfo(CoreInfo::intInfo)));
      return CBExposedTypesInfo(_consumedInfo);
    }
  }

  ALWAYS_INLINE CBVar activate(CBContext *context, const CBVar &input) {
    auto repeats = _forever ? 1 : _times;

    if (_ctxVar.size()) {
      if (!_ctxTimes)
        _ctxTimes = contextVariable(context, _ctxVar.c_str());
      repeats = _ctxTimes->payload.intValue;
    }

    while (repeats) {
      CBVar repeatOutput{};
      repeatOutput.valueType = None;
      repeatOutput.payload.chainState = CBChainState::Continue;
      auto state = activateBlocks(_blocks.payload.seqValue, context, input,
                                  repeatOutput);
      if (unlikely(state == FlowState::Stopping)) {
        return StopChain;
      } else if (unlikely(state == FlowState::Returning)) {
        break;
      }

      if (!_forever)
        repeats--;
    }
    return input;
  }
};

RUNTIME_CORE_BLOCK_TYPE(Const);
RUNTIME_CORE_BLOCK_TYPE(Input);
RUNTIME_CORE_BLOCK_TYPE(Sleep);
RUNTIME_CORE_BLOCK_TYPE(And);
RUNTIME_CORE_BLOCK_TYPE(Or);
RUNTIME_CORE_BLOCK_TYPE(Not);
RUNTIME_CORE_BLOCK_TYPE(Stop);
RUNTIME_CORE_BLOCK_TYPE(Restart);
RUNTIME_CORE_BLOCK_TYPE(Return);
RUNTIME_CORE_BLOCK_TYPE(IsValidNumber);
RUNTIME_CORE_BLOCK_TYPE(Set);
RUNTIME_CORE_BLOCK_TYPE(Ref);
RUNTIME_CORE_BLOCK_TYPE(Update);
RUNTIME_CORE_BLOCK_TYPE(Get);
RUNTIME_CORE_BLOCK_TYPE(Swap);
RUNTIME_CORE_BLOCK_TYPE(Take);
RUNTIME_CORE_BLOCK_TYPE(Limit);
RUNTIME_CORE_BLOCK_TYPE(Push);
RUNTIME_CORE_BLOCK_TYPE(Pop);
RUNTIME_CORE_BLOCK_TYPE(Clear);
RUNTIME_CORE_BLOCK_TYPE(Count);
RUNTIME_CORE_BLOCK_TYPE(Repeat);
}; // namespace chainblocks