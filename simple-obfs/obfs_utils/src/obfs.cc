
#include "obfs_utils/obfs.h"

using ObfsGenerator = ObfsGeneratorFactory::ObfsGenerator;

 std::shared_ptr<const ObfsArgs> Obfuscator::kArgs = nullptr;

boost::optional<ObfsGenerator>
    ObfsGeneratorFactory::GetGenerator(std::string name) {
        auto itr = generator_functions_.find(name);
        if (itr == generator_functions_.end()) {
            return boost::none;
        }
        return itr->second;
    }

std::shared_ptr<ObfsGeneratorFactory>
    ObfsGeneratorFactory::Instance() {
        static std::shared_ptr<ObfsGeneratorFactory>
                        self(new ObfsGeneratorFactory);
        return self;
    }

void ObfsGeneratorFactory::GetAllRegisteredNames(std::vector<std::string> &names) {
    names.resize(generator_functions_.size());
    std::transform(
        std::begin(generator_functions_),
        std::end(generator_functions_),
        names.begin(),
        [](const auto &kv) { return kv.first; }
    );
}

