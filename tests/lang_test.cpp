// Checks the UI language files against the code; run from the repository root.
//  * every T(L"...") / TF(L"...") phrase in src/ is listed in lang/_template.txt (fails otherwise);
//  * a translation uses only the {placeholders} of its English phrase (fails otherwise);
//  * a translated phrase that is no longer in the template is reported as stale (fails: the English
//    text changed and the translation has to move to the new phrase);
//  * prints how many phrases each language still leaves in English (does not fail).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\xEF\xBB\xBF"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// "key = value" lines like i18n.cpp reads them; "key =" (nothing translated) gives an empty value.
std::map<std::string, std::string> Phrases(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '@') continue;
        size_t eq = line.find(" = ");
        if (eq != std::string::npos)
            out[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 3));
        else if (line.size() > 2 && line.compare(line.size() - 2, 2, " =") == 0)
            out[Trim(line.substr(0, line.size() - 2))] = "";
    }
    return out;
}

std::set<std::string> Placeholders(const std::string& s) {
    std::set<std::string> out;
    for (size_t a = s.find('{'); a != std::string::npos; a = s.find('{', a + 1)) {
        size_t b = s.find('}', a);
        if (b == std::string::npos) break;
        out.insert(s.substr(a, b - a + 1));
    }
    return out;
}

// String literals passed to T(L"...") or TF(L"...") in the sources.
std::set<std::string> CodePhrases() {
    std::set<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator("src")) {
        auto ext = e.path().extension();
        if (ext != ".cpp" && ext != ".h") continue;
        std::string text = Read(e.path());
        for (const char* call : { "T(L\"", "TF(L\"", "tr(L\"" }) {
            for (size_t at = text.find(call); at != std::string::npos; at = text.find(call, at + 1)) {
                if (at > 0 && (isalnum((unsigned char)text[at - 1]) || text[at - 1] == '_')) continue;
                size_t start = at + strlen(call), end = start;
                while (end < text.size() && text[end] != '"') end += text[end] == '\\' ? 2 : 1;
                if (end > start) out.insert(text.substr(start, end - start));
            }
        }
    }
    return out;
}

}  // namespace

int main() {
    int failures = 0;
    const auto tmpl = Phrases(Read("lang/_template.txt"));
    if (tmpl.empty()) {
        std::printf("lang/_template.txt not found or empty (run from the repository root)\n");
        return 1;
    }
    for (const auto& phrase : CodePhrases())
        if (!tmpl.count(phrase)) {
            std::printf("FAIL: \"%s\" is used in src/ but missing from lang/_template.txt\n", phrase.c_str());
            ++failures;
        }

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator("lang"))
        if (e.path().extension() == ".txt" && e.path().filename() != "_template.txt") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    std::printf("Translation coverage (%zu phrases):", tmpl.size());
    for (const auto& file : files) {
        const auto phrases = Phrases(Read(file));
        size_t translated = 0;
        for (const auto& [en, tr] : phrases) {
            if (tr.empty()) continue;
            if (!tmpl.count(en)) {
                std::printf("\nFAIL: %s: stale phrase \"%s\" (not in the template)", file.filename().string().c_str(),
                            en.c_str());
                ++failures;
                continue;
            }
            ++translated;
            const auto allowed = Placeholders(en);
            for (const auto& p : Placeholders(tr))
                if (!allowed.count(p)) {
                    std::printf("\nFAIL: %s: \"%s\" uses %s, which its English phrase does not have",
                                file.filename().string().c_str(), en.c_str(), p.c_str());
                    ++failures;
                }
        }
        std::printf(" %s %zu/%zu", file.stem().string().c_str(), translated, tmpl.size());
    }
    std::printf("\n");
    if (failures) return 1;
    std::printf("lang_test passed\n");
    return 0;
}
