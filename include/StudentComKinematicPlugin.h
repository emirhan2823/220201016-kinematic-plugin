#pragma once

#include <plugin/IPlugin.h>

#include <string>
#include <vector>

namespace arkheon::astsim {
class ModelFactoryRegistry;
}

namespace student::comkinematic {

class StudentComKinematicPlugin final : public arkheon::astlib::IPlugin {
public:
    StudentComKinematicPlugin() = default;
    ~StudentComKinematicPlugin() override = default;

    [[nodiscard]] int getInterfaceVersion() const override;
    [[nodiscard]] arkheon::astlib::PluginMetadata getMetadata() const override;

    void initialize(arkheon::astlib::PluginContext& context) override;
    void tick(double dt) override;
    void shutdown() override;

private:
    bool initialized_ = false;
    bool shutdown_ = false;
    std::string modelType_ = "animationModelNathanHuman";
    std::vector<std::string> registeredAnimationCodes_;
    arkheon::astsim::ModelFactoryRegistry* modelFactoryRegistry_ = nullptr;
};

} // namespace student::comkinematic

extern "C" {
ARKHEON_ASTLIB_API arkheon::astlib::IPlugin* create_plugin();
ARKHEON_ASTLIB_API void destroy_plugin(arkheon::astlib::IPlugin* plugin);
ARKHEON_ASTLIB_API const char* get_plugin_signature();
}
