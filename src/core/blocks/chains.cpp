/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019 Giovanni Petrantoni */

#include "rigtorp/SPSCQueue.h"
#include "shared.hpp"
#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace chainblocks {
enum RunChainMode { Inline, Detached, Stepped };

struct ChainBase {
  static inline TypesInfo chainTypes =
      TypesInfo::FromMany(false, CBType::Chain, CBType::None);

  static inline ParamsInfo waitChainParamsInfo = ParamsInfo(
      ParamsInfo::Param("Chain", "The chain to run.", CBTypesInfo(chainTypes)),
      ParamsInfo::Param("Once",
                        "Runs this sub-chain only once within the parent chain "
                        "execution cycle.",
                        CBTypesInfo(SharedTypes::boolInfo)),
      ParamsInfo::Param(
          "Passthrough",
          "The input of this block will be the output. Always on if Detached.",
          CBTypesInfo(SharedTypes::boolInfo)));

  static inline ParamsInfo runChainParamsInfo = ParamsInfo(
      ParamsInfo::Param("Chain", "The chain to run.", CBTypesInfo(chainTypes)),
      ParamsInfo::Param("Once",
                        "Runs this sub-chain only once within the parent chain "
                        "execution cycle.",
                        CBTypesInfo(SharedTypes::boolInfo)),
      ParamsInfo::Param(
          "Passthrough",
          "The input of this block will be the output. Not used if Detached.",
          CBTypesInfo(SharedTypes::boolInfo)),
      ParamsInfo::Param(
          "Mode",
          "The way to run the chain. Inline: will run the sub chain inline "
          "within the root chain, a pause in the child chain will pause the "
          "root "
          "too; Detached: will run the chain separately in the same node, a "
          "pause in this chain will not pause the root; Stepped: the chain "
          "will "
          "run as a child, the root will tick the chain every activation of "
          "this "
          "block and so a child pause won't pause the root.",
          CBTypesInfo(SharedTypes::runChainModeInfo)));

  static inline ParamsInfo chainloaderParamsInfo = ParamsInfo(
      ParamsInfo::Param(
          "File", "The chainblocks lisp file of the chain to run and watch.",
          CBTypesInfo(SharedTypes::strInfo)),
      ParamsInfo::Param("Once",
                        "Runs this sub-chain only once within the parent chain "
                        "execution cycle.",
                        CBTypesInfo(SharedTypes::boolInfo)),
      ParamsInfo::Param(
          "Mode",
          "The way to run the chain. Inline: will run the sub chain inline "
          "within the root chain, a pause in the child chain will pause the "
          "root "
          "too; Detached: will run the chain separately in the same node, a "
          "pause in this chain will not pause the root; Stepped: the chain "
          "will "
          "run as a child, the root will tick the chain every activation of "
          "this "
          "block and so a child pause won't pause the root.",
          CBTypesInfo(SharedTypes::runChainModeInfo)));

  static inline ParamsInfo chainOnlyParamsInfo = ParamsInfo(
      ParamsInfo::Param("Chain", "The chain to run.", CBTypesInfo(chainTypes)));

  CBChain *chain;
  bool once;
  bool doneOnce;
  bool passthrough;
  RunChainMode mode;
  CBValidationResult chainValidation{};

  void destroy() { stbds_arrfree(chainValidation.exposedInfo); }

  static CBTypesInfo inputTypes() { return CBTypesInfo(SharedTypes::anyInfo); }
  static CBTypesInfo outputTypes() { return CBTypesInfo(SharedTypes::anyInfo); }

  CBTypeInfo inferTypes(CBTypeInfo inputType, CBExposedTypesInfo consumables) {
    // Free any previous result!
    stbds_arrfree(chainValidation.exposedInfo);
    chainValidation.exposedInfo = nullptr;

    // Easy case, no chain...
    if (!chain)
      return inputType;

    // We need to validate the sub chain to figure it out!
    chainValidation = validateConnections(
        chain,
        [](const CBlock *errorBlock, const char *errorTxt, bool nonfatalWarning,
           void *userData) {
          if (!nonfatalWarning) {
            LOG(ERROR) << "RunChain: failed inner chain validation, error: "
                       << errorTxt;
            throw CBException("RunChain: failed inner chain validation");
          } else {
            LOG(INFO) << "RunChain: warning during inner chain validation: "
                      << errorTxt;
          }
        },
        this, inputType,
        mode == RunChainMode::Inline
            ? consumables
            : nullptr); // detached don't share context!

    return passthrough || mode == RunChainMode::Detached
               ? inputType
               : chainValidation.outputType;
  }
};

struct WaitChain : public ChainBase {
  void cleanup() { doneOnce = false; }

  static CBParametersInfo parameters() {
    return CBParametersInfo(waitChainParamsInfo);
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      chain = value.payload.chainValue;
      break;
    case 1:
      once = value.payload.boolValue;
      break;
    case 2:
      passthrough = value.payload.boolValue;
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(chain);
    case 1:
      return Var(once);
    case 2:
      return Var(passthrough);
    default:
      break;
    }
    return Var();
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (unlikely(!chain)) {
      return input;
    } else if (!doneOnce) {
      if (once)
        doneOnce = true;

      while (!hasEnded(chain)) {
        cbpause(0.0);
      }

      return passthrough ? input : chain->finishedOutput;
    } else {
      return input;
    }
  }
};

struct ChainRunner : public ChainBase {
  // Only chain runners should expose varaibles to the context
  CBExposedTypesInfo exposedVariables() {
    return mode == RunChainMode::Inline ? chainValidation.exposedInfo : nullptr;
  }

  void cleanup() {
    if (chain)
      chainblocks::stop(chain);
    doneOnce = false;
  }
};

struct RunChain : public ChainRunner {
  static CBParametersInfo parameters() {
    return CBParametersInfo(runChainParamsInfo);
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      chain = value.payload.chainValue;
      break;
    case 1:
      once = value.payload.boolValue;
      break;
    case 2:
      passthrough = value.payload.boolValue;
      break;
    case 3:
      mode = RunChainMode(value.payload.enumValue);
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(chain);
    case 1:
      return Var(once);
    case 2:
      return Var(passthrough);
    case 3:
      return Var::Enum(mode, 'frag', 'runC');
    default:
      break;
    }
    return Var();
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (unlikely(!chain))
      return input;

    if (!doneOnce) {
      if (once)
        doneOnce = true;

      if (mode == RunChainMode::Detached) {
        if (!chainblocks::isRunning(chain)) {
          // validated during infer
          context->chain->node->schedule(chain, input, false);
        }
        return input;
      } else if (mode == RunChainMode::Stepped) {
        // Allow to re run chains
        if (chainblocks::hasEnded(chain)) {
          chainblocks::stop(chain);
        }
        // Prepare if no callc was called
        if (!chain->coro) {
          chainblocks::prepare(chain);
        }
        // Ticking or starting
        if (!chainblocks::isRunning(chain)) {
          chainblocks::start(chain, input);
        } else {
          chainblocks::tick(chain, input);
        }
        return passthrough ? input : chain->previousOutput;
      } else {
        // Run within the root flow
        auto runRes = runSubChain(chain, context, input);
        if (unlikely(runRes.state == Failed || context->aborted)) {
          return StopChain;
        } else if (passthrough) {
          return input;
        } else {
          return runRes.output;
        }
      }
    } else {
      return input;
    }
  }
};

struct ChainLoadResult {
  bool hasError;
  std::string errorMsg;
  CBChain *chain;
  void *env;
};

struct ChainFileWatcher {
  std::atomic_bool running;
  std::thread worker;
  std::string fileName;
  rigtorp::SPSCQueue<ChainLoadResult> results;
  boost::lockfree::queue<void *> envs_gc;

  explicit ChainFileWatcher(std::string &file)
      : running(true), fileName(file), results(2), envs_gc(2) {
    worker = std::thread([this] {
      decltype(fs::last_write_time(fs::path())) lastWrite{};
      if (!Lisp::Create) {
        LOG(ERROR) << "Failed to load lisp interpreter";
        return;
      }

      while (running) {
        try {
          fs::path p(fileName);
          if (fs::exists(p) && fs::is_regular_file(p) &&
              fs::last_write_time(p) != lastWrite) {
            // make sure to store last write time
            // before any possible error!
            auto writeTime = fs::last_write_time(p);
            lastWrite = writeTime;

            std::ifstream lsp(p.c_str());
            std::string str((std::istreambuf_iterator<char>(lsp)),
                            std::istreambuf_iterator<char>());
            auto env = Lisp::Create();
            auto v = Lisp::Eval(env, str.c_str());
            if (v.valueType != Chain) {
              LOG(ERROR) << "Lisp::Eval did not return a CBChain";
              Lisp::Destroy(env);
              continue;
            }

            auto chain = v.payload.chainValue;

            // run validation to infertypes and specialize
            auto chainValidation = validateConnections(
                chain,
                [](const CBlock *errorBlock, const char *errorTxt,
                   bool nonfatalWarning, void *userData) {
                  if (!nonfatalWarning) {
                    auto msg =
                        "RunChain: failed inner chain validation, error: " +
                        std::string(errorTxt);
                    throw chainblocks::CBException(msg);
                  } else {
                    LOG(INFO)
                        << "RunChain: warning during inner chain validation: "
                        << errorTxt;
                  }
                },
                nullptr);
            stbds_arrfree(chainValidation.exposedInfo);

            ChainLoadResult result = {false, "", chain, env};
            results.push(result);
          }

          // clean garbage if any
          if (!envs_gc.empty()) {
            void *env;
            if (envs_gc.pop(env)) {
              Lisp::Destroy(env);
            }
          }

          // sleep some
          chainblocks::sleep(2.0);
        } catch (std::exception &e) {
          ChainLoadResult result = {true, e.what(), nullptr, nullptr};
          results.push(result);
        } catch (...) {
          ChainLoadResult result = {true, "general error", nullptr, nullptr};
          results.push(result);
        }
      }
    });
  }

  ~ChainFileWatcher() {
    running = false;
    worker.join();
  }
};

struct ChainLoader : public ChainRunner {
  std::string fileName;
  std::unique_ptr<ChainFileWatcher> watcher;
  void *currentEnv;

  ChainLoader() : ChainRunner(), watcher(nullptr), currentEnv(nullptr) {}

  static CBParametersInfo parameters() {
    return CBParametersInfo(chainloaderParamsInfo);
  }

  void setParam(int index, CBVar value) {
    switch (index) {
    case 0:
      fileName = value.payload.stringValue;
      watcher.reset(new ChainFileWatcher(fileName));
      break;
    case 1:
      once = value.payload.boolValue;
      break;
    case 2:
      mode = RunChainMode(value.payload.enumValue);
      break;
    default:
      break;
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(fileName);
      break;
    case 1:
      return Var(once);
      break;
    case 2:
      return Var::Enum(mode, 'frag', 'runC');
      break;
    default:
      break;
    }
    return Var();
  }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (!watcher->results.empty()) {
      auto result = watcher->results.front();
      if (unlikely(result->hasError)) {
        LOG(ERROR) << "Failed to reload a chain via ChainLoader, reason: "
                   << result->errorMsg;
      } else {
        if (chain) {
          chainblocks::stop(chain);
          // don't delete chains.. let env do it
          while (!watcher->envs_gc.push(currentEnv)) {
            cbpause(0.0);
          }
        }
        chain = result->chain;
        currentEnv = result->env;
      }
      watcher->results.pop();
    }

    if (unlikely(!chain))
      return input;

    if (!doneOnce) {
      if (once)
        doneOnce = true;

      if (mode == RunChainMode::Detached) {
        if (!chainblocks::isRunning(chain)) {
          // validated during infer
          context->chain->node->schedule(chain, input, false);
        }
        return input;
      } else if (mode == RunChainMode::Stepped) {
        // Allow to re run chains
        if (chainblocks::hasEnded(chain)) {
          chainblocks::stop(chain);
        }
        // Prepare if no callc was called
        if (!chain->coro) {
          chainblocks::prepare(chain);
        }
        // Ticking or starting
        if (!chainblocks::isRunning(chain)) {
          chainblocks::start(chain, input);
        } else {
          chainblocks::tick(chain, input);
        }
        return input;
      } else {
        // Run within the root flow
        auto runRes = runSubChain(chain, context, input);
        if (unlikely(runRes.state == Failed || context->aborted)) {
          return StopChain;
        } else {
          return input;
        }
      }
    } else {
      return input;
    }
  }
};

#define CHAIN_BLOCK_DEF(NAME)                                                  \
  RUNTIME_CORE_BLOCK(NAME);                                                    \
  RUNTIME_BLOCK_inputTypes(NAME);                                              \
  RUNTIME_BLOCK_outputTypes(NAME);                                             \
  RUNTIME_BLOCK_parameters(NAME);                                              \
  RUNTIME_BLOCK_inferTypes(NAME);                                              \
  RUNTIME_BLOCK_exposedVariables(NAME);                                        \
  RUNTIME_BLOCK_setParam(NAME);                                                \
  RUNTIME_BLOCK_getParam(NAME);                                                \
  RUNTIME_BLOCK_activate(NAME);                                                \
  RUNTIME_BLOCK_cleanup(NAME);                                                 \
  RUNTIME_BLOCK_destroy(NAME);                                                 \
  RUNTIME_BLOCK_END(NAME);

// Register
CHAIN_BLOCK_DEF(RunChain);
CHAIN_BLOCK_DEF(ChainLoader);

RUNTIME_CORE_BLOCK(WaitChain);
RUNTIME_BLOCK_inputTypes(WaitChain);
RUNTIME_BLOCK_outputTypes(WaitChain);
RUNTIME_BLOCK_parameters(WaitChain);
RUNTIME_BLOCK_inferTypes(WaitChain);
RUNTIME_BLOCK_setParam(WaitChain);
RUNTIME_BLOCK_getParam(WaitChain);
RUNTIME_BLOCK_activate(WaitChain);
RUNTIME_BLOCK_cleanup(WaitChain);
RUNTIME_BLOCK_destroy(WaitChain);
RUNTIME_BLOCK_END(WaitChain);

void registerChainsBlocks() {
  REGISTER_CORE_BLOCK(RunChain);
  REGISTER_CORE_BLOCK(ChainLoader);
  REGISTER_CORE_BLOCK(WaitChain);
}
}; // namespace chainblocks
