/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2022 Fragcolor Pte. Ltd. */

use crate::core::registerShard;
use crate::shard::Shard;
use crate::types::Context;
use crate::types::ExposedInfo;
use crate::types::ExposedTypes;
use crate::types::ParamVar;
use crate::types::Type;
use crate::types::Var;
use crate::types::FRAG_CC;
use crate::types::NONE_TYPES;
use crate::types::{RawString, Types};
use egui::CentralPanel as EguiCentralPanel;
use egui::Context as EguiNativeContext;
use std::rc::Rc;

static EGUI_UI_TYPE: Type = Type::object(FRAG_CC, 1701279061); // 'eguU'
static EGUI_UI_SLICE: &'static [Type] = &[EGUI_UI_TYPE];
static EGUI_UI_VAR: Type = Type::context_variable(EGUI_UI_SLICE);

static EGUI_CTX_TYPE: Type = Type::object(FRAG_CC, 1701279043); // 'eguC'
static EGUI_CTX_SLICE: &'static [Type] = &[EGUI_CTX_TYPE];
static EGUI_CTX_VAR: Type = Type::context_variable(EGUI_CTX_SLICE);

lazy_static! {
  static ref EGUI_CTX_VEC: Types = vec![EGUI_CTX_TYPE];
}

// The root of a GUI tree.
// This could be flat on the screen, or it could be attached to 3D geometry.
struct EguiContext {
  context: Rc<Option<EguiNativeContext>>,
}

impl Default for EguiContext {
  fn default() -> Self {
    Self {
      context: Rc::new(None),
    }
  }
}

impl Shard for EguiContext {
  fn registerName() -> &'static str {
    cstr!("GUI")
  }

  fn hash() -> u32 {
    compile_time_crc32::crc32!("GUI-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "GUI"
  }

  fn inputTypes(&mut self) -> &std::vec::Vec<Type> {
    &NONE_TYPES
  }

  fn outputTypes(&mut self) -> &std::vec::Vec<Type> {
    &EGUI_CTX_VEC
  }

  fn warmup(&mut self, _ctx: &Context) -> Result<(), &str> {
    self.context = Rc::new(Some(EguiNativeContext::default()));
    Ok(())
  }

  fn activate(&mut self, _: &Context, _input: &Var) -> Result<Var, &str> {
    let result = Var::new_object(&self.context, &EGUI_CTX_TYPE);
    Ok(result)
  }
}

// ! A panel that covers the remainder of the screen, i.e. whatever area is left after adding other panels.
struct Panels {
  context: Option<Rc<Option<EguiNativeContext>>>,
  instance: ParamVar, // Context parameter, this will go will with trait system (users able to plug into existing UIs and interop with them)
  requiring: ExposedTypes,
}

impl Default for Panels {
  fn default() -> Self {
    Self {
      context: None,
      instance: ParamVar::new(().into()),
      requiring: Vec::new(),
    }
  }
}

impl Shard for Panels {
  fn registerName() -> &'static str {
    cstr!("GUI.Panels")
  }

  fn hash() -> u32 {
    compile_time_crc32::crc32!("GUI.Panels-rust-0x20200101")
  }

  fn name(&mut self) -> &str {
    "GUI.Panels"
  }

  fn inputTypes(&mut self) -> &std::vec::Vec<Type> {
    &NONE_TYPES
  }

  fn outputTypes(&mut self) -> &std::vec::Vec<Type> {
    &NONE_TYPES
  }

  fn requiredVariables(&mut self) -> Option<&ExposedTypes> {
    self.requiring.clear();
    let exp_info = ExposedInfo {
      exposedType: EGUI_CTX_TYPE,
      name: shstr!("GUI.Root"),
      help: cstr!("The exposed GUI root context.").into(),
      ..ExposedInfo::default()
    };
    self.requiring.push(exp_info);
    Some(&self.requiring)
  }

  fn warmup(&mut self, _ctx: &Context) -> Result<(), &str> {
    self.context = Some(Var::from_object_as_clone(
      self.instance.get(),
      &EGUI_CTX_TYPE,
    )?);
    Ok(())
  }

  fn cleanup(&mut self) {
    self.context = None; // release RC etc
  }

  fn activate(&mut self, _: &Context, input: &Var) -> Result<Var, &str> {
    let gui_ctx = Var::get_mut_from_clone(&self.context)?;
    EguiCentralPanel::default().show(gui_ctx, |ui| {

    });
    Ok(*input)
  }
}

pub fn registerShards() {
  registerShard::<EguiContext>();
  registerShard::<Panels>();
}
