#include <ruby/ruby.hpp>
//using namespace ruby;

#include <hiro/hiro.hpp>
using namespace hiro;

#include <emulator/emulator.hpp>

#include <nall/instance.hpp>
#include <nall/beat/single/apply.hpp>

namespace ruby {
  extern Video video;
  extern Audio audio;
  extern Input input;
}

namespace byuu {
  static const string Name      = "byuu";
  static const string Version   = "110";
  static const string Copyright = "byuu et al";
  static const string License   = "GPLv3 or later";
  static const string Website   = "https://github.com/higan-emu";
}

#include "resource/resource.hpp"
#include "emulator/emulator.hpp"
#include "program/program.hpp"
#include "input/input.hpp"
#include "presentation/presentation.hpp"
#include "settings/settings.hpp"
#include "tools/tools.hpp"

auto locate(const string& name) -> string;
