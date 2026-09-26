#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace inop {
namespace gui {

class InterfaceLanguages {
public:
    InterfaceLanguages() : names_{"English"}, codes_{"eng"} {}

    bool add_catalog(const std::string& code, const std::string& name,
                     std::map<std::string, std::string> entries) {
        if (code.empty() || name.empty() || code == "eng" || has(code)) return false;
        codes_.push_back(code);
        names_.push_back(name);
        catalogs_.emplace(code, std::move(entries));
        return true;
    }

    bool has(const std::string& code) const {
        for (const std::string& known : codes_)
            if (known == code) return true;
        return false;
    }

    std::string supported_code(const std::string& code) const {
        return has(code) ? code : "eng";
    }

    int index_of(const std::string& code) const {
        for (std::size_t i = 0; i < codes_.size(); ++i)
            if (codes_[i] == code) return static_cast<int>(i);
        return 0;
    }

    const std::string& code_at(int index) const {
        if (index < 0 || index >= static_cast<int>(codes_.size())) return codes_.front();
        return codes_[static_cast<std::size_t>(index)];
    }

    const std::vector<std::string>& names() const { return names_; }

    std::string lookup(const std::string& code, const std::string& english) const {
        auto catalog = catalogs_.find(code);
        if (catalog == catalogs_.end()) return english;
        auto entry = catalog->second.find(english);
        return entry != catalog->second.end() && !entry->second.empty()
                   ? entry->second : english;
    }

private:
    std::vector<std::string> names_;
    std::vector<std::string> codes_;
    std::map<std::string, std::map<std::string, std::string>> catalogs_;
};

inline InterfaceLanguages& interface_languages() {
    static InterfaceLanguages languages;
    return languages;
}

}
}
