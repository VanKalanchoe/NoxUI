#include "NoxUI/Core/UICommandList.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace NoxUI
{
    namespace
    {
        std::string Trim(std::string text)
        {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
            text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
            return text;
        }
    }

    std::string UIKeyChord::ToString() const
    {
        if (!IsSet())
            return {};
        std::string text;
        if (Ctrl) text += "Ctrl+";
        if (Shift) text += "Shift+";
        if (Alt) text += "Alt+";
        return text + GetKeyName(Key);
    }

    UIKeyChord UIKeyChord::Parse(const std::string& source)
    {
        UIKeyChord chord;
        std::string text = Trim(source);
        if (text.empty())
            return chord;
        // Modifiers first ("+" alone is the key "+" only at the end).
        for (;;)
        {
            if (text.rfind("Ctrl+", 0) == 0) { chord.Ctrl = true; text.erase(0, 5); }
            else if (text.rfind("Shift+", 0) == 0) { chord.Shift = true; text.erase(0, 6); }
            else if (text.rfind("Alt+", 0) == 0) { chord.Alt = true; text.erase(0, 4); }
            else break;
        }
        for (int32_t key = static_cast<int32_t>(UIKey::A); key <= static_cast<int32_t>(UIKey::CapsLock); ++key)
            if (text == GetKeyName(static_cast<UIKey>(key)))
            {
                chord.Key = static_cast<UIKey>(key);
                return chord;
            }
        return {};
    }

    UICommand& UICommandList::Add(UICommand command)
    {
        command.Key = command.DefaultKey;
        command.AltKey = command.DefaultAltKey;
        if (UICommand* existing = FindMutable(command.Id))
        {
            *existing = std::move(command);
            return *existing;
        }
        m_Commands.push_back(std::move(command));
        return m_Commands.back();
    }

    const UICommand* UICommandList::Find(const std::string& id) const
    {
        for (const UICommand& command : m_Commands)
            if (command.Id == id)
                return &command;
        return nullptr;
    }

    UICommand* UICommandList::FindMutable(const std::string& id)
    {
        return const_cast<UICommand*>(Find(id));
    }

    bool UICommandList::CanExecute(const std::string& id) const
    {
        const UICommand* command = Find(id);
        return command && command->Execute && (!command->CanExecute || command->CanExecute());
    }

    bool UICommandList::Execute(const std::string& id) const
    {
        if (!CanExecute(id))
            return false;
        const UICommand* command = Find(id);
        const std::function<void()> run = command->Execute; // (the command may rebuild the list it is in)
        run();
        return true;
    }

    std::string UICommandList::ShortcutText(const std::string& id) const
    {
        const UICommand* command = Find(id);
        if (!command)
            return {};
        std::string text = command->Key.ToString();
        if (command->AltKey.IsSet())
            text += (text.empty() ? "" : " / ") + command->AltKey.ToString();
        return text;
    }

    bool UIKeyChord::TypesText() const
    {
        return !Ctrl && !Alt && !(Key >= UIKey::F1 && Key <= UIKey::F12);
    }

    const UICommand* UICommandList::FindForKey(const UIKeyChord& chord, const std::function<bool(const UICommand&)>& allowed) const
    {
        if (!chord.IsSet())
            return nullptr;
        for (const UICommand& command : m_Commands)
        {
            if (!(command.Key == chord) && !(command.AltKey == chord))
                continue;
            if (allowed && !allowed(command))
                continue;
            // (one key can serve several commands -- Esc ends the measure tool, else stops Play: the first that can run now)
            if (command.CanExecute && !command.CanExecute())
                continue;
            return &command;
        }
        return nullptr;
    }

    std::string UICommandList::Bind(const std::string& id, const UIKeyChord& chord, bool primary)
    {
        UICommand* command = FindMutable(id);
        if (!command)
            return {};
        std::string taken;
        if (chord.IsSet())
        {
            for (UICommand& other : m_Commands)
            {
                if (&other == command)
                    continue;
                if (other.Key == chord) { other.Key = {}; taken = other.Id; }
                if (other.AltKey == chord) { other.AltKey = {}; taken = other.Id; }
            }
        }
        (primary ? command->Key : command->AltKey) = chord;
        return taken;
    }

    void UICommandList::ResetToDefault(const std::string& id)
    {
        UICommand* command = FindMutable(id);
        if (!command)
            return;
        // The defaults may be taken by another command by now: taken back.
        Bind(id, command->DefaultKey, true);
        Bind(id, command->DefaultAltKey, false);
    }

    void UICommandList::ResetAll()
    {
        for (UICommand& command : m_Commands)
        {
            command.Key = command.DefaultKey;
            command.AltKey = command.DefaultAltKey;
        }
    }

    bool UICommandList::Load(const std::filesystem::path& path)
    {
        ResetAll();
        std::ifstream in(path);
        if (!in)
            return false;
        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            UICommand* command = FindMutable(Trim(line.substr(0, colon)));
            if (!command)
                continue; // (a command this editor no longer has)
            const std::string keys = line.substr(colon + 1);
            const size_t bar = keys.find('|');
            command->Key = UIKeyChord::Parse(keys.substr(0, bar));
            command->AltKey = bar == std::string::npos ? UIKeyChord{} : UIKeyChord::Parse(keys.substr(bar + 1));
        }
        return true;
    }

    bool UICommandList::Save(const std::filesystem::path& path, const std::string& header) const
    {
        std::ostringstream out;
        out << header << "\n";
        bool any = false;
        for (const UICommand& command : m_Commands)
        {
            if (command.Key == command.DefaultKey && command.AltKey == command.DefaultAltKey)
                continue;
            out << command.Id << ": " << command.Key.ToString() << " | " << command.AltKey.ToString() << "\n";
            any = true;
        }
        std::error_code error;
        if (!any)
        {
            std::filesystem::remove(path, error);
            return true;
        }
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::trunc);
        if (!file)
            return false;
        file << out.str();
        return static_cast<bool>(file);
    }
}
