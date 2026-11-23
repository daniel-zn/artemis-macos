#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <vector>

// Mac keyboard shortcuts on the host (Settings > Input > "Use Mac keyboard shortcuts"), the way
// Parallels, VMware Fusion, Windows App and Jump Desktop do it: Command works as Ctrl, so Cmd+C,
// Cmd+V, Cmd+Z, Cmd+F, Cmd+S, Cmd+W and the rest do what they do on a Mac, and the few Mac text
// shortcuts that aren't Ctrl+key on Windows are translated:
//
//   Cmd+Left / Cmd+Right        Home / End
//   Cmd+Up / Cmd+Down           Ctrl+Home / Ctrl+End
//   Cmd+Backspace               Shift+Home, Backspace (delete to the start of the line)
//   Shift+Cmd+Z                 Ctrl+Y (redo)
//   Option+Left / Option+Right  Ctrl+Left / Ctrl+Right (a word at a time)
//   Option+Backspace / Delete   Ctrl+Backspace / Ctrl+Delete (delete a word)
//   Cmd+Tab                     Alt+Tab (only reaches the stream when system keys are captured)
//   Ctrl+Cmd+key                Win+key (Ctrl+Cmd+R = Win+R, Ctrl+Cmd+D = Win+D)
//
// Shift passes through (Shift+Cmd+Left = Shift+Home). Ctrl stays the real Ctrl and Option stays
// Alt. Cmd+Q stays Artemis's (it ends the session, see quitduringstream.h) and Ctrl+Alt+Shift
// combos stay Artemis's: neither reaches this class.
//
// Works on Windows virtual-key codes, after SdlInputHandler has turned the scancode into one.
// The host's modifier keys follow the Mac's (Cmd presses Ctrl at once, so Cmd+click is
// Ctrl+click); for a translated key they change to the shortcut's, and change back at the next
// key or modifier press. That keeps Option+Left from leaving a lone Alt tap (which would open the
// host app's menu bar) when Option goes up.
class MacShortcuts
{
public:
    struct HostKey {
        short vk;
        bool down;
        uint8_t modifiers;  // MODIFIER_* bits, including MODIFIER_EXTENDED for this key
        bool operator==(const HostKey& o) const { return vk == o.vk && down == o.down && modifiers == o.modifiers; }
    };

    // A key on the Mac keyboard as a Windows VK (Cmd is VK_LWIN/VK_RWIN). `extended` is the key's
    // MODIFIER_EXTENDED; the host gets the returned keys in order.
    std::vector<HostKey> key(short vk, bool down, bool extended);

    // Everything was released (focus lost, a key combo); start from nothing held
    void reset();

private:
    std::set<short> desiredModifiers() const;
    void sync(const std::set<short>& desired, std::vector<HostKey>& out);
    uint8_t hostModifierBits() const;
    void send(short vk, bool down, bool extended, std::vector<HostKey>& out);

    std::set<short> m_Physical;        // modifier keys held on the Mac
    std::set<short> m_Host;            // modifier keys held on the host
    std::map<short, short> m_Pressed;  // Mac key -> host key it pressed, for its key-up
    bool m_CmdTab = false;             // Cmd+Tab: Cmd is Alt until it goes up
};
