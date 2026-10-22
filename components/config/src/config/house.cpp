#include "house.h"

#include <string.h>

namespace config {

static constexpr uint8_t BLOB_VERSION = 1;
static constexpr uint8_t FLAG_INVERT_ENDS = 0x01;
static constexpr uint8_t FLAG_INVERT_MID = 0x02;

const char *describe(Error e) {
  switch (e) {
    case Error::None: return "ok";
    case Error::BadIndex: return "no such shutter";
    case Error::BadName: return "name must be 1-32 bytes without control characters";
    case Error::BadGroup: return "group must be 1-7";
    case Error::BadMember: return "member must be 1-7";
    case Error::DuplicateAddress: return "another shutter already uses this group and member";
    case Error::BadTimes: return "travel times must be 1000-300000 ms";
    case Error::BadCentralId: return "2411 ID must be 0x80xxxx";
    case Error::BadRepeats: return "repeats must be 1-8";
    case Error::BadBlob: return "stored configuration is unreadable";
  }
  return "?";
}

House defaults() {
  static const char *const NAMES[] = {"Raum 1 links", "Raum 1 rechts", "Raum 2 links", "Raum 2 rechts",
                                      "Raum 3 links", "Raum 3 rechts", "Raum 4 links", "Raum 4 rechts"};
  House h;
  h.count = 8;
  for (uint8_t i = 0; i < h.count; i++) {
    Shutter &s = h.shutters[i];
    strncpy(s.name, NAMES[i], MAX_NAME_BYTES);
    s.group = (uint8_t)(i / 2 + 1);
    s.member = (uint8_t)(i % 2 + 1);
  }
  return h;
}

static bool validName(const char *name) {
  const size_t len = strnlen(name, MAX_NAME_BYTES + 1);
  if (len == 0 || len > MAX_NAME_BYTES) return false;
  for (size_t i = 0; i < len; i++) {
    if ((uint8_t)name[i] < 0x20 || name[i] == 0x7f) return false;
  }
  return true;
}

static Error checkShutter(const Shutter &s) {
  if (!validName(s.name)) return Error::BadName;
  if (s.group < 1 || s.group > 7) return Error::BadGroup;
  if (s.member < 1 || s.member > 7) return Error::BadMember;
  if (s.upMs < MIN_TRAVEL_MS || s.upMs > MAX_TRAVEL_MS || s.downMs < MIN_TRAVEL_MS || s.downMs > MAX_TRAVEL_MS) {
    return Error::BadTimes;
  }
  return Error::None;
}

Error validate(const House &h) {
  if (((h.centralId >> 16) & 0xf0) != 0x80 || h.centralId > 0xffffff) return Error::BadCentralId;
  if (h.repeats < 1 || h.repeats > MAX_REPEATS) return Error::BadRepeats;
  if (h.count > MAX_SHUTTERS) return Error::BadIndex;
  for (size_t i = 0; i < h.count; i++) {
    const Error e = checkShutter(h.shutters[i]);
    if (e != Error::None) return e;
    for (size_t j = 0; j < i; j++) {
      if (h.shutters[j].group == h.shutters[i].group && h.shutters[j].member == h.shutters[i].member) {
        return Error::DuplicateAddress;
      }
    }
  }
  return Error::None;
}

// Applies `edit` to a copy and keeps it only if the whole house stays valid.
template <typename Edit>
static Error update(House &h, size_t index, Edit edit) {
  if (index >= h.count) return Error::BadIndex;
  House copy = h;
  edit(copy.shutters[index]);
  const Error e = validate(copy);
  if (e == Error::None) h = copy;
  return e;
}

Error setName(House &h, size_t index, const char *name) {
  if (!validName(name)) return Error::BadName;
  return update(h, index, [name](Shutter &s) {
    memset(s.name, 0, sizeof(s.name));
    strncpy(s.name, name, MAX_NAME_BYTES);
  });
}

Error setAddress(House &h, size_t index, uint8_t group, uint8_t member) {
  return update(h, index, [group, member](Shutter &s) {
    s.group = group;
    s.member = member;
  });
}

Error setTimes(House &h, size_t index, uint32_t upMs, uint32_t downMs) {
  return update(h, index, [upMs, downMs](Shutter &s) {
    s.upMs = upMs;
    s.downMs = downMs;
  });
}

Error setInvert(House &h, size_t index, bool mid, bool on) {
  return update(h, index, [mid, on](Shutter &s) { (mid ? s.invertMid : s.invertEnds) = on; });
}

Error setCentralId(House &h, uint32_t id) {
  House copy = h;
  copy.centralId = id;
  const Error e = validate(copy);
  if (e == Error::None) h = copy;
  return e;
}

Error setRepeats(House &h, uint8_t repeats) {
  House copy = h;
  copy.repeats = repeats;
  const Error e = validate(copy);
  if (e == Error::None) h = copy;
  return e;
}

// --- Blob --------------------------------------------------------------------
//
//   u8 version, u8 count, u8 repeats, u8 reserved, u32 centralId
//   per shutter: name[33], u8 group, u8 member, u32 upMs, u32 downMs, u8 flags

namespace {

struct Writer {
  uint8_t *p;
  uint8_t *end;
  bool ok = true;
  void u8(uint8_t v) {
    if (p >= end) ok = false;
    else *p++ = v;
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; i++) u8((uint8_t)(v >> (8 * i)));
  }
  void bytes(const void *src, size_t n) {
    for (size_t i = 0; i < n; i++) u8(((const uint8_t *)src)[i]);
  }
};

struct Reader {
  const uint8_t *p;
  const uint8_t *end;
  bool ok = true;
  uint8_t u8() {
    if (p >= end) {
      ok = false;
      return 0;
    }
    return *p++;
  }
  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i);
    return v;
  }
  void bytes(void *dst, size_t n) {
    for (size_t i = 0; i < n; i++) ((uint8_t *)dst)[i] = u8();
  }
};

}  // namespace

size_t serialize(const House &h, uint8_t *out, size_t cap) {
  Writer w{out, out + cap};
  w.u8(BLOB_VERSION);
  w.u8(h.count);
  w.u8(h.repeats);
  w.u8(0);
  w.u32(h.centralId);
  for (size_t i = 0; i < h.count && i < MAX_SHUTTERS; i++) {
    const Shutter &s = h.shutters[i];
    w.bytes(s.name, MAX_NAME_BYTES + 1);
    w.u8(s.group);
    w.u8(s.member);
    w.u32(s.upMs);
    w.u32(s.downMs);
    w.u8((uint8_t)((s.invertEnds ? FLAG_INVERT_ENDS : 0) | (s.invertMid ? FLAG_INVERT_MID : 0)));
  }
  return w.ok ? (size_t)(w.p - out) : 0;
}

Error deserialize(const uint8_t *data, size_t len, House &out) {
  Reader r{data, data + len};
  if (r.u8() != BLOB_VERSION) return Error::BadBlob;
  House h;
  h.count = r.u8();
  h.repeats = r.u8();
  r.u8();
  h.centralId = r.u32();
  if (h.count > MAX_SHUTTERS) return Error::BadBlob;
  for (size_t i = 0; i < h.count; i++) {
    Shutter &s = h.shutters[i];
    r.bytes(s.name, MAX_NAME_BYTES + 1);
    s.name[MAX_NAME_BYTES] = '\0';
    s.group = r.u8();
    s.member = r.u8();
    s.upMs = r.u32();
    s.downMs = r.u32();
    const uint8_t flags = r.u8();
    s.invertEnds = flags & FLAG_INVERT_ENDS;
    s.invertMid = flags & FLAG_INVERT_MID;
  }
  if (!r.ok || r.p != r.end) return Error::BadBlob;
  const Error e = validate(h);
  if (e != Error::None) return e;
  out = h;
  return Error::None;
}

}  // namespace config
