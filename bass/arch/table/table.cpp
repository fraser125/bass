#define arch(name) static string Arch_##name
#include "nes-cpu.arch"
#include "pce-cpu.arch"
#include "snes-cpu.arch"
#include "snes-smp.arch"
#include "snes-gsu.arch"
#include "gb-cpu.arch"
#include "gg-cpu.arch"
#include "sms-cpu.arch"
#include "msx-cpu.arch"
#include "msxtr-cpu.arch"
#undef arch

auto BassTable::initialize() -> void {
  Bass::initialize();

  bitval = 0;
  bitpos = 0;
  table.reset();
}

auto BassTable::assemble(const string& statement) -> bool {
  if(Bass::assemble(statement) == true) return true;

  string s = statement;

  if(s.match("arch ?*")) {
    s.trimLeft("arch ", 1L);
    string data;
    if(0);
    else if(s == "reset") data = "";
    else if(s == "nes.cpu") data = Arch_nes_cpu;
    else if(s == "pce.cpu") data = Arch_pce_cpu;
    else if(s == "snes.cpu") data = Arch_snes_cpu;
    else if(s == "snes.smp") data = Arch_snes_smp;
    else if(s == "snes.gsu") data = Arch_snes_gsu;
    else if(s == "gb.cpu") data = Arch_gb_cpu;
    else if(s == "gg.cpu") data = Arch_gg_cpu;
    else if(s == "sms.cpu") data = Arch_sms_cpu;
    else if(s == "msx.cpu") data = Arch_msx_cpu;
    else if(s == "msxtr.cpu") data = Arch_msxtr_cpu;
    else if(s.match("\"?*\"")) {
      s.trim("\"", "\"", 1L);
      s = {pathname(sourceFilenames.right()), s};
      if(!file::exists(s)) error("arch file ", s, " not found");
      data = file::read(s);
    } else {
      error("unrecognized arch ", s);
    }
    table.reset();
    parseTable(data);
    return true;
  }

  if(s.match("instrument \"*\"")) {
    s.trim("instrument \"", "\"", 1L);
    parseTable(s);
    return true;
  }

  uint pc = this->pc();

  for(auto& opcode : table) {
    if(tokenize(s, opcode.pattern) == false) continue;

    string_vector args;
    tokenize(args, s, opcode.pattern);
    if(args.size() != opcode.number.size()) continue;

    bool mismatch = false;
    for(auto& format : opcode.format) {
      if(format.type == Format::Type::Absolute) {
        if(format.match != Format::Match::Weak) {
          uint bits = bitLength(args[format.argument]);
          if(bits != opcode.number[format.argument].bits) {
            if(format.match == Format::Match::Exact || bits != 0) {
              mismatch = true;
              break;
            }
          }
        }
      }
    }
    if(mismatch) continue;

    for(auto& format : opcode.format) {
      switch(format.type) {
        case Format::Type::Static: {
          writeBits(format.data, format.bits);
          break;
        }

        case Format::Type::Absolute: {
          uint data = evaluate(args[format.argument]);
          writeBits(data, opcode.number[format.argument].bits);
          break;
        }

        case Format::Type::Relative: {
          int data = evaluate(args[format.argument]) - (pc + format.displacement);
          uint bits = opcode.number[format.argument].bits;
          int min = -(1 << (bits - 1)), max = +(1 << (bits - 1)) - 1;
          if(data < min || data > max) error("branch out of bounds");
          writeBits(data, opcode.number[format.argument].bits);
          break;
        }

        case Format::Type::Repeat: {
          uint data = evaluate(args[format.argument]);
          for(uint n : range(data)) {
            writeBits(format.data, opcode.number[format.argument].bits);
          }
          break;
        }
      }
    }

    return true;
  }

  return false;
}

auto BassTable::bitLength(string& text) const -> uint {
  auto binLength = [&](const char* p) -> uint {
    uint length = 0;
    while(*p) {
      if(*p == '0' || *p == '1') { p++; length += 1; continue; }
      return 0;
    }
    return length;
  };

  auto hexLength = [&](const char* p) -> uint {
    uint length = 0;
    while(*p) {
      if(*p >= '0' && *p <= '9') { p++; length += 4; continue; }
      if(*p >= 'a' && *p <= 'f') { p++; length += 4; continue; }
      if(*p >= 'A' && *p <= 'F') { p++; length += 4; continue; }
      return 0;
    }
    return length;
  };

  char* p = text.get();
  if(*p == '<') { *p = ' '; return  8; }
  if(*p == '>') { *p = ' '; return 16; }
  if(*p == '^') { *p = ' '; return 24; }
  if(*p == '?') { *p = ' '; return 32; }
  if(*p == ':') { *p = ' '; return 64; }
  if(*p == '%') return binLength(p + 1);
  if(*p == '$') return hexLength(p + 1);
  if(*p == '0' && *(p + 1) == 'b') return binLength(p + 2);
  if(*p == '0' && *(p + 1) == 'x') return hexLength(p + 2);
  return 0;
}

auto BassTable::writeBits(uint64_t data, uint length) -> void {
  bitval <<= length;
  bitval |= data;
  bitpos += length;

  while(bitpos >= 8) {
    write(bitval);
    bitval >>= 8;
    bitpos -= 8;
  }
}

auto BassTable::parseTable(const string& text) -> bool {
  auto lines = text.split("\n");
  for(auto& line : lines) {
    if(auto position = line.find("//")) line.resize(position());  //remove comments
    auto part = line.split(";", 1L).strip();
    if(part.size() != 2) continue;

    Opcode opcode;
    assembleTableLHS(opcode, part(0));
    assembleTableRHS(opcode, part(1));
    table.append(opcode);
  }

  return true;
}

auto BassTable::assembleTableLHS(Opcode& opcode, const string& text) -> void {
  uint offset = 0;

  auto length = [&] {
    uint length = 0;
    while(text[offset + length]) {
      char n = text[offset + length];
      if(n == '*') break;
      length++;
    }
    return length;
  };

  while(text[offset]) {
    uint size = length();
    opcode.prefix.append({slice(text, offset, size), size});
    offset += size;

    if(text[offset] != '*') continue;
    uint bits = 10 * (text[offset + 1] - '0');
    bits += text[offset + 2] - '0';
    opcode.number.append({bits});
    offset += 3;
  }

  for(auto& prefix : opcode.prefix) {
    opcode.pattern.append(prefix.text, "*");
  }
  opcode.pattern.trimRight("*", 1L);
  if(opcode.number.size() == opcode.prefix.size()) opcode.pattern.append("*");
}

auto BassTable::assembleTableRHS(Opcode& opcode, const string& text) -> void {
  uint offset = 0;

  auto list = text.split(" ");
  for(auto& item : list) {
    if(item[0] == '$' && item.length() == 3) {
      Format format = {Format::Type::Static};
      format.data = toHex((const char*)item + 1);
      format.bits = (item.length() - 1) * 4;
      opcode.format.append(format);
    }

    if(item[0] == '%') {
      Format format = {Format::Type::Static};
      format.data = toBinary((const char*)item + 1);
      format.bits = (item.length() - 1);
      opcode.format.append(format);
    }

    if(item[0] == '!') {
      Format format = {Format::Type::Absolute, Format::Match::Exact};
      format.argument = item[1] - 'a';
      opcode.format.append(format);
    }

    if(item[0] == '=') {
      Format format = {Format::Type::Absolute, Format::Match::Strong};
      format.argument = item[1] - 'a';
      opcode.format.append(format);
    }

    if(item[0] == '~') {
      Format format = {Format::Type::Absolute, Format::Match::Weak};
      format.argument = item[1] - 'a';
      opcode.format.append(format);
    }

    if(item[0] == '+') {
      Format format = {Format::Type::Relative};
      format.argument = item[2] - 'a';
      format.displacement = +(item[1] - '0');
      opcode.format.append(format);
    }

    if(item[0] == '-') {
      Format format = {Format::Type::Relative};
      format.argument = item[2] - 'a';
      format.displacement = -(item[1] - '0');
      opcode.format.append(format);
    }

    if(item[0] == '*') {
      Format format = {Format::Type::Repeat};
      format.argument = item[1] - 'a';
      format.data = toHex((const char*)item + 3);
      opcode.format.append(format);
    }
  }
}
