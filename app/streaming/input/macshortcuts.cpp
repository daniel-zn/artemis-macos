#include "macshortcuts.h"

// From Limelight.h, kept here so this class (and its unit test) builds without it
static constexpr uint8_t ModShift = 0x01, ModCtrl = 0x02, ModAlt = 0x04, ModMeta = 0x08, ModExtended = 0x10;

enum : short {
    VkBack = 0x08, VkTab = 0x09, VkEnd = 0x23, VkHome = 0x24,
    VkLeft = 0x25, VkUp = 0x26, VkRight = 0x27, VkDown = 0x28, VkDelete = 0x2E,
    VkY = 0x59, VkZ = 0x5A, VkLWin = 0x5B, VkRWin = 0x5C,
    VkLShift = 0xA0, VkRShift = 0xA1, VkLCtrl = 0xA2, VkRCtrl = 0xA3, VkLAlt = 0xA4, VkRAlt = 0xA5,
};

static bool isModifier(short vk)
{
    return (vk >= VkLShift && vk <= VkRAlt) || vk == VkLWin || vk == VkRWin;
}

static bool isExtended(short vk)
{
    switch (vk) {
    case VkEnd: case VkHome: case VkLeft: case VkUp: case VkRight: case VkDown: case VkDelete:
    case VkLWin: case VkRWin: case VkRCtrl: case VkRAlt:
        return true;
    default:
        return false;
    }
}

// The translated shortcuts. `cmd`: the shortcut is Cmd+key, else Option+key.
// `shift`: Shift must be held (and is dropped on the host).
struct Rule {
    bool cmd;
    short key;
    bool shift;
    short hostKey;
    uint8_t hostMods;     // ModCtrl/ModAlt/ModShift; Shift held on the Mac is added unless `shift`
    short tapFirst;       // a key tapped (with hostMods) before hostKey goes down, or 0
};

static const Rule Rules[] = {
    { true,  VkLeft,   false, VkHome,   0,        0 },
    { true,  VkRight,  false, VkEnd,    0,        0 },
    { true,  VkUp,     false, VkHome,   ModCtrl,  0 },
    { true,  VkDown,   false, VkEnd,    ModCtrl,  0 },
    { true,  VkBack,   false, VkBack,   ModShift, VkHome },
    { true,  VkZ,      true,  VkY,      ModCtrl,  0 },
    { true,  VkTab,    false, VkTab,    ModAlt,   0 },
    { false, VkLeft,   false, VkLeft,   ModCtrl,  0 },
    { false, VkRight,  false, VkRight,  ModCtrl,  0 },
    { false, VkBack,   false, VkBack,   ModCtrl,  0 },
    { false, VkDelete, false, VkDelete, ModCtrl,  0 },
};

void MacShortcuts::send(short vk, bool down, bool extended, std::vector<HostKey>& out)
{
    if (isModifier(vk)) {
        if (down) {
            m_Host.insert(vk);
        }
        else {
            m_Host.erase(vk);
        }
    }
    out.push_back({ vk, down, (uint8_t)(hostModifierBits() | (extended ? ModExtended : 0)) });
}

uint8_t MacShortcuts::hostModifierBits() const
{
    uint8_t bits = 0;
    for (short vk : m_Host) {
        switch (vk) {
        case VkLShift: case VkRShift: bits |= ModShift; break;
        case VkLCtrl: case VkRCtrl: bits |= ModCtrl; break;
        case VkLAlt: case VkRAlt: bits |= ModAlt; break;
        case VkLWin: case VkRWin: bits |= ModMeta; break;
        }
    }
    return bits;
}

// The host modifiers that match the Mac's: Cmd is Ctrl (Alt after Cmd+Tab), Ctrl+Cmd is Win
std::set<short> MacShortcuts::desiredModifiers() const
{
    bool lCmd = m_Physical.count(VkLWin), rCmd = m_Physical.count(VkRWin);
    bool lCtrl = m_Physical.count(VkLCtrl), rCtrl = m_Physical.count(VkRCtrl);
    std::set<short> want;
    for (short vk : { VkLShift, VkRShift, VkLAlt, VkRAlt }) {
        if (m_Physical.count(vk)) {
            want.insert(vk);
        }
    }
    if ((lCmd || rCmd) && (lCtrl || rCtrl)) {
        want.insert(VkLWin);
    }
    else if (m_CmdTab) {
        want.insert(VkLAlt);
    }
    else {
        if (lCtrl || lCmd) want.insert(VkLCtrl);
        if (rCtrl || rCmd) want.insert(VkRCtrl);
    }
    return want;
}

// Presses before releases, so a modifier swap never leaves the host with a lone Alt or Win tap
void MacShortcuts::sync(const std::set<short>& desired, std::vector<HostKey>& out)
{
    for (short vk : desired) {
        if (!m_Host.count(vk)) {
            send(vk, true, isExtended(vk), out);
        }
    }
    std::set<short> held = m_Host;
    for (short vk : held) {
        if (!desired.count(vk)) {
            send(vk, false, isExtended(vk), out);
        }
    }
}

std::vector<MacShortcuts::HostKey> MacShortcuts::key(short vk, bool down, bool extended)
{
    std::vector<HostKey> out;

    if (isModifier(vk)) {
        if (down) {
            m_Physical.insert(vk);
        }
        else {
            m_Physical.erase(vk);
        }
        if (!m_Physical.count(VkLWin) && !m_Physical.count(VkRWin)) {
            m_CmdTab = false;
        }
        sync(desiredModifiers(), out);
        return out;
    }

    if (!down) {
        auto it = m_Pressed.find(vk);
        short hostKey = it != m_Pressed.end() ? it->second : vk;
        if (it != m_Pressed.end()) {
            m_Pressed.erase(it);
        }
        send(hostKey, false, hostKey == vk ? extended : isExtended(hostKey), out);
        return out;
    }

    bool cmd = m_Physical.count(VkLWin) || m_Physical.count(VkRWin);
    bool ctrl = m_Physical.count(VkLCtrl) || m_Physical.count(VkRCtrl);
    bool alt = m_Physical.count(VkLAlt) || m_Physical.count(VkRAlt);
    bool shift = m_Physical.count(VkLShift) || m_Physical.count(VkRShift);

    const Rule* rule = nullptr;
    if (!ctrl && cmd != alt) {
        for (const Rule& r : Rules) {
            if (r.cmd == cmd && r.key == vk && r.shift == shift) {
                rule = &r;
                break;
            }
        }
        // Shift passes through, except where the rule itself is a Shift shortcut
        if (rule == nullptr && shift) {
            for (const Rule& r : Rules) {
                if (r.cmd == cmd && r.key == vk && !r.shift && r.tapFirst == 0) {
                    rule = &r;
                    break;
                }
            }
        }
    }

    if (rule == nullptr) {
        sync(desiredModifiers(), out);
        m_Pressed[vk] = vk;
        send(vk, true, extended, out);
        return out;
    }

    if (rule->key == VkTab) {
        m_CmdTab = true;
    }
    std::set<short> want;
    if (rule->hostMods & ModCtrl) want.insert(VkLCtrl);
    if (rule->hostMods & ModAlt) want.insert(VkLAlt);
    if (rule->hostMods & ModShift) want.insert(VkLShift);
    for (short s : { VkLShift, VkRShift }) {
        if (!rule->shift && m_Physical.count(s)) want.insert(s);
    }
    sync(want, out);
    if (rule->tapFirst != 0) {
        send(rule->tapFirst, true, isExtended(rule->tapFirst), out);
        send(rule->tapFirst, false, isExtended(rule->tapFirst), out);
    }
    m_Pressed[vk] = rule->hostKey;
    send(rule->hostKey, true, isExtended(rule->hostKey), out);
    return out;
}

void MacShortcuts::reset()
{
    m_Physical.clear();
    m_Host.clear();
    m_Pressed.clear();
    m_CmdTab = false;
}
