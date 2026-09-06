#ifndef SAS_TRANSLATION_H
#define SAS_TRANSLATION_H

#include <string>
#include "json.hpp"

using nlohmann::json;

class Translation {
    public:
        Translation();
        ~Translation();
        static Translation& instance() {
            return *translationInstance;
        };

        std::string translate(const std::string& key);
        bool startup = true;
        void setLanguage(const std::string& language);
        void loadTranslations();

    private:
        static Translation* translationInstance;
        json translations;
        std::string currentLanguage = "en";
};

#define t(key) Translation::instance().translate(key).c_str()

#endif // SAS_TRANSLATION_H