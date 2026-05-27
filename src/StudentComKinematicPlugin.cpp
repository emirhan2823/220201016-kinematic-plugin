#include "StudentComKinematicPlugin.h"

#include <model/AnimationModel.h>
#include <model/ModelFactoryRegistry.h>
#include <plugin/IModelPluginService.h>
#include <plugin/IPluginServices.h>
#include <plugin/PluginContext.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace student::comkinematic {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kStepFrequencyHz = 1.25;
constexpr double kStepWidthM = 0.24;
constexpr double kStrideM = 0.62;
constexpr double kThighM = 0.45;
constexpr double kShinM = 0.45;

struct ContactState {
    double leftFootWeight = 1.0;
    double rightFootWeight = 1.0;
    double supportX = 0.0;
    double supportZ = 0.0;
};

struct KinematicPose {
    double phase = 0.0;
    double leftHipPitch = 0.0;
    double rightHipPitch = 0.0;
    double leftKnee = 0.0;
    double rightKnee = 0.0;
    double leftAnkle = 0.0;
    double rightAnkle = 0.0;
    double pelvisX = 0.0;
    double pelvisZ = 0.0;
    double trunkPitch = 0.0;
    double trunkRoll = 0.0;
    double armSwing = 0.0;
};

struct CenterOfMass {
    double x = 0.0;
    double z = 0.0;
};

[[nodiscard]] double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(value, hi));
}

[[nodiscard]] bool hasJoint(const std::unordered_set<std::string>& availableJointIds, const char* jointId)
{
    if (!jointId || *jointId == '\0') {
        return false;
    }
    if (availableJointIds.empty()) {
        return true;
    }
    return availableJointIds.find(jointId) != availableJointIds.end();
}

[[nodiscard]] double smoothStep(double edge0, double edge1, double x)
{
    const double t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] ContactState inferContactState(double phase)
{
    ContactState contact;
    contact.leftFootWeight = 1.0 - smoothStep(0.55, 0.70, phase);
    if (phase >= 0.85) {
        contact.leftFootWeight = smoothStep(0.85, 1.0, phase);
    }

    double rightBase = 0.0;
    if (phase < 0.15) {
        rightBase = 1.0 - smoothStep(0.05, 0.15, phase);
    } else if (phase >= 0.42) {
        rightBase = smoothStep(0.42, 0.55, phase);
    }
    contact.rightFootWeight = rightBase;

    const double denom = std::max(0.01, contact.leftFootWeight + contact.rightFootWeight);
    contact.supportX = ((-kStepWidthM * 0.5) * contact.leftFootWeight
                      + ( kStepWidthM * 0.5) * contact.rightFootWeight) / denom;
    contact.supportZ = 0.0;
    return contact;
}

[[nodiscard]] KinematicPose buildNominalWalk(double simulationTimeSeconds)
{
    KinematicPose pose;
    pose.phase = std::fmod(simulationTimeSeconds * kStepFrequencyHz, 1.0);
    if (pose.phase < 0.0) {
        pose.phase += 1.0;
    }

    const double cycle = pose.phase * 2.0 * kPi;
    const double stride = std::sin(cycle);
    const double liftLeft = std::max(0.0, std::sin(cycle));
    const double liftRight = std::max(0.0, -std::sin(cycle));

    pose.leftHipPitch = 0.34 * stride;
    pose.rightHipPitch = -0.34 * stride;
    pose.leftKnee = 0.18 + 0.58 * liftLeft;
    pose.rightKnee = 0.18 + 0.58 * liftRight;
    pose.leftAnkle = -0.12 - 0.25 * liftLeft;
    pose.rightAnkle = -0.12 - 0.25 * liftRight;
    pose.pelvisX = 0.035 * std::sin(cycle + kPi * 0.5);
    pose.pelvisZ = 0.035 * std::sin(cycle);
    pose.trunkPitch = -0.08 + 0.04 * std::sin(cycle + kPi * 0.5);
    pose.trunkRoll = -0.08 * std::sin(cycle + kPi * 0.5);
    pose.armSwing = -0.36 * stride;
    return pose;
}

[[nodiscard]] CenterOfMass estimateCenterOfMass(const KinematicPose& pose)
{
    double weightedX = 0.0;
    double weightedZ = 0.0;
    double mass = 0.0;

    const auto addMass = [&](double segmentMass, double x, double z) {
        weightedX += segmentMass * x;
        weightedZ += segmentMass * z;
        mass += segmentMass;
    };

    const double leftHipX = pose.pelvisX - kStepWidthM * 0.32;
    const double rightHipX = pose.pelvisX + kStepWidthM * 0.32;
    const double trunkX = pose.pelvisX + 0.08 * std::sin(pose.trunkRoll);
    const double trunkZ = pose.pelvisZ + 0.12 * std::sin(-pose.trunkPitch);

    addMass(0.46, trunkX, trunkZ + 0.03);
    addMass(0.14, pose.pelvisX, pose.pelvisZ);
    addMass(0.08, trunkX, trunkZ + 0.05);

    const double leftThighZ = pose.pelvisZ + std::sin(pose.leftHipPitch) * kThighM * 0.5;
    const double rightThighZ = pose.pelvisZ + std::sin(pose.rightHipPitch) * kThighM * 0.5;
    const double leftShinZ = pose.pelvisZ + std::sin(pose.leftHipPitch - pose.leftKnee) * kShinM * 0.5;
    const double rightShinZ = pose.pelvisZ + std::sin(pose.rightHipPitch - pose.rightKnee) * kShinM * 0.5;

    addMass(0.10, leftHipX, leftThighZ);
    addMass(0.10, rightHipX, rightThighZ);
    addMass(0.06, leftHipX, leftShinZ);
    addMass(0.06, rightHipX, rightShinZ);

    return {weightedX / mass, weightedZ / mass};
}

void applyBalanceCorrection(KinematicPose& pose, const ContactState& contact)
{
    const CenterOfMass com = estimateCenterOfMass(pose);
    const double lateralError = contact.supportX - com.x;
    const double foreAftError = contact.supportZ - com.z;

    const double trunkRollCorrection = clamp(lateralError * 0.75, -0.12, 0.12);
    const double trunkPitchCorrection = clamp(foreAftError * 0.55, -0.10, 0.10);

    pose.trunkRoll += trunkRollCorrection;
    pose.trunkPitch += trunkPitchCorrection;
    pose.leftHipPitch += trunkPitchCorrection * 0.20;
    pose.rightHipPitch += trunkPitchCorrection * 0.20;

    const double singleSupportBias = contact.leftFootWeight - contact.rightFootWeight;
    pose.pelvisX -= singleSupportBias * 0.015;
}

void addOverrideIfAvailable(arkheon::astsim::AnimationModelOutput& output,
                            const std::unordered_set<std::string>& availableJointIds,
                            const char* jointId,
                            double xRad,
                            double yRad,
                            double zRad)
{
    if (hasJoint(availableJointIds, jointId)) {
        output.jointOverrides.push_back({jointId, xRad, yRad, zRad});
    }
}

[[nodiscard]] bool evaluateStudentComWalk(const arkheon::astsim::AnimationModelInput& input,
                                          arkheon::astsim::AnimationModelOutput& output)
{
    std::unordered_set<std::string> availableJointIds;
    availableJointIds.reserve(input.entity.joints.size());
    for (const auto& joint : input.entity.joints) {
        availableJointIds.insert(joint.jointId);
    }

    KinematicPose pose = buildNominalWalk(input.simulationTimeSeconds);
    const ContactState contact = inferContactState(pose.phase);
    applyBalanceCorrection(pose, contact);

    const double cycle = pose.phase * 2.0 * kPi;

    const double armSwingSmooth = pose.armSwing;
    const double elbowBendDynamic = 0.5 * (1.0 - std::cos(2.0 * cycle)) * 0.10;
    const double shoulderBob = 0.06 * std::sin(2.0 * cycle);

    output.clearExistingJointOverrides = false;
    output.jointOverrides.clear();

    addOverrideIfAvailable(output, availableJointIds, "spineLower", pose.trunkPitch * 0.55, pose.trunkRoll * 0.35, -pose.trunkRoll * 0.70);
    addOverrideIfAvailable(output, availableJointIds, "spineUpper", pose.trunkPitch * 0.45, pose.trunkRoll * 0.55, -pose.trunkRoll * 0.45);
    addOverrideIfAvailable(output, availableJointIds, "head", -pose.trunkPitch * 0.45, 0.0, pose.trunkRoll * 0.35);

    addOverrideIfAvailable(output, availableJointIds, "leftHip", pose.leftHipPitch, pose.trunkRoll * 0.30, -0.04);
    addOverrideIfAvailable(output, availableJointIds, "rightHip", pose.rightHipPitch, pose.trunkRoll * 0.30, 0.04);
    addOverrideIfAvailable(output, availableJointIds, "leftKnee", pose.leftKnee, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightKnee", pose.rightKnee, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "leftAnkle", pose.leftAnkle, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightAnkle", pose.rightAnkle, 0.0, 0.0);

    addOverrideIfAvailable(output, availableJointIds, "leftShoulder", 0.22 + shoulderBob, 0.0, -armSwingSmooth);
    addOverrideIfAvailable(output, availableJointIds, "rightShoulder", 0.22 - shoulderBob, 0.0, armSwingSmooth);
    addOverrideIfAvailable(output, availableJointIds, "leftElbow", 0.48 + elbowBendDynamic, 0.0, -0.06);
    addOverrideIfAvailable(output, availableJointIds, "rightElbow", 0.48 + elbowBendDynamic, 0.0, 0.06);

    return !output.jointOverrides.empty();
}

[[nodiscard]] bool evaluateStudentComPush(const arkheon::astsim::AnimationModelInput& input,
                                          arkheon::astsim::AnimationModelOutput& output)
{
    std::unordered_set<std::string> availableJointIds;
    availableJointIds.reserve(input.entity.joints.size());
    for (const auto& joint : input.entity.joints) {
        availableJointIds.insert(joint.jointId);
    }

    const double t = input.simulationTimeSeconds;
    const double pulse = 0.5 + 0.5 * std::sin(t * 2.8);
    const double counter = std::sin(t * 2.8 + kPi * 0.5);
    const double trunkPitch = -0.34 - 0.08 * pulse;
    const double reach = 0.92 + 0.16 * pulse;
    const double kneeBend = 0.42 + 0.12 * counter;
    const double ankleCounter = -0.22 - 0.08 * counter;

    output.clearExistingJointOverrides = false;
    output.jointOverrides.clear();

    addOverrideIfAvailable(output, availableJointIds, "spineLower", trunkPitch * 0.65, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "spineUpper", trunkPitch * 0.45, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "head", -trunkPitch * 0.35, 0.0, 0.0);

    addOverrideIfAvailable(output, availableJointIds, "leftHip", -0.22, 0.06, -0.04);
    addOverrideIfAvailable(output, availableJointIds, "rightHip", -0.22, -0.06, 0.04);
    addOverrideIfAvailable(output, availableJointIds, "leftKnee", kneeBend, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightKnee", kneeBend, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "leftAnkle", ankleCounter, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightAnkle", ankleCounter, 0.0, 0.0);

    addOverrideIfAvailable(output, availableJointIds, "leftShoulder", reach, 0.18, -0.22);
    addOverrideIfAvailable(output, availableJointIds, "rightShoulder", reach, -0.18, 0.22);
    addOverrideIfAvailable(output, availableJointIds, "leftElbow", 0.36 + 0.16 * (1.0 - pulse), 0.0, -0.10);
    addOverrideIfAvailable(output, availableJointIds, "rightElbow", 0.36 + 0.16 * (1.0 - pulse), 0.0, 0.10);

    return !output.jointOverrides.empty();
}

[[nodiscard]] bool evaluateStudentComClimb(const arkheon::astsim::AnimationModelInput& input,
                                           arkheon::astsim::AnimationModelOutput& output)
{
    std::unordered_set<std::string> availableJointIds;
    availableJointIds.reserve(input.entity.joints.size());
    for (const auto& joint : input.entity.joints) {
        availableJointIds.insert(joint.jointId);
    }

    const double t = input.simulationTimeSeconds;
    const double cycle = std::sin(t * 2.1);
    const double liftLeft = std::max(0.0, cycle);
    const double liftRight = std::max(0.0, -cycle);
    const double trunkPitch = -0.28 + 0.06 * std::sin(t * 4.2);

    output.clearExistingJointOverrides = false;
    output.jointOverrides.clear();

    addOverrideIfAvailable(output, availableJointIds, "spineLower", trunkPitch * 0.55, 0.05 * cycle, -0.08 * cycle);
    addOverrideIfAvailable(output, availableJointIds, "spineUpper", trunkPitch * 0.45, -0.06 * cycle, -0.05 * cycle);
    addOverrideIfAvailable(output, availableJointIds, "head", -trunkPitch * 0.35, 0.0, 0.04 * cycle);

    addOverrideIfAvailable(output, availableJointIds, "leftHip", 0.42 + 0.66 * liftLeft, 0.08, -0.03);
    addOverrideIfAvailable(output, availableJointIds, "rightHip", 0.42 + 0.66 * liftRight, -0.08, 0.03);
    addOverrideIfAvailable(output, availableJointIds, "leftKnee", 0.62 + 0.72 * liftLeft, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightKnee", 0.62 + 0.72 * liftRight, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "leftAnkle", -0.32 - 0.18 * liftLeft, 0.0, 0.0);
    addOverrideIfAvailable(output, availableJointIds, "rightAnkle", -0.32 - 0.18 * liftRight, 0.0, 0.0);

    addOverrideIfAvailable(output, availableJointIds, "leftShoulder", 0.62 + 0.24 * liftRight, 0.12, -0.42);
    addOverrideIfAvailable(output, availableJointIds, "rightShoulder", 0.62 + 0.24 * liftLeft, -0.12, 0.42);
    addOverrideIfAvailable(output, availableJointIds, "leftElbow", 0.86, 0.0, -0.22);
    addOverrideIfAvailable(output, availableJointIds, "rightElbow", 0.86, 0.0, 0.22);

    return !output.jointOverrides.empty();
}

} // namespace

int StudentComKinematicPlugin::getInterfaceVersion() const
{
    return 1;
}

arkheon::astlib::PluginMetadata StudentComKinematicPlugin::getMetadata() const
{
    arkheon::astlib::PluginMetadata metadata;
    metadata.setPluginId("student-com-kinematic-plugin");
    metadata.setVersion("0.1.0");
    metadata.setAuthor("Student");
    metadata.addCapability("animation:nathan-human:student-com-walk");
    metadata.addCapability("animation:nathan-human:student-com-push");
    metadata.addCapability("animation:nathan-human:student-com-climb");
    return metadata;
}

void StudentComKinematicPlugin::initialize(arkheon::astlib::PluginContext& context)
{
    initialized_ = true;
    shutdown_ = false;
    registeredAnimationCodes_.clear();
    modelFactoryRegistry_ = nullptr;

    if (context.services) {
        auto* rawService = context.services->getService(arkheon::astsim::IModelPluginService::kPluginServiceId);
        auto* service = static_cast<arkheon::astsim::IModelPluginService*>(rawService);
        modelFactoryRegistry_ = service ? &service->modelFactoryRegistry() : nullptr;
    }
    if (!modelFactoryRegistry_) {
        return;
    }

    auto* prototypeBase = modelFactoryRegistry_->getRegisteredPrototype(modelType_);
    auto* prototypeAnimationModel = dynamic_cast<arkheon::astsim::IAnimationModel*>(prototypeBase);
    if (!prototypeAnimationModel) {
        return;
    }

    const auto registerAnimation = [&](const std::string& code,
                                       arkheon::astsim::IAnimationModel::AnimationEvaluationFunction evaluator) {
        if (prototypeAnimationModel->registerAnimation(code, std::move(evaluator))) {
            registeredAnimationCodes_.push_back(code);
        }
    };

    registerAnimation("Student CoM Walk", evaluateStudentComWalk);
    registerAnimation("Student CoM Push", evaluateStudentComPush);
    registerAnimation("Student CoM Climb", evaluateStudentComClimb);
}

void StudentComKinematicPlugin::tick(double dt)
{
    static_cast<void>(dt);
}

void StudentComKinematicPlugin::shutdown()
{
    if (modelFactoryRegistry_ && !registeredAnimationCodes_.empty()) {
        auto* prototypeBase = modelFactoryRegistry_->getRegisteredPrototype(modelType_);
        auto* prototypeAnimationModel = dynamic_cast<arkheon::astsim::IAnimationModel*>(prototypeBase);
        if (prototypeAnimationModel) {
            for (const auto& code : registeredAnimationCodes_) {
                static_cast<void>(prototypeAnimationModel->registerAnimation(
                    code,
                    arkheon::astsim::IAnimationModel::AnimationEvaluationFunction {}));
            }
        }
    }

    registeredAnimationCodes_.clear();
    shutdown_ = true;
    modelFactoryRegistry_ = nullptr;
}

} // namespace student::comkinematic

extern "C" {

ARKHEON_ASTLIB_API arkheon::astlib::IPlugin* create_plugin()
{
    return new student::comkinematic::StudentComKinematicPlugin();
}

ARKHEON_ASTLIB_API void destroy_plugin(arkheon::astlib::IPlugin* plugin)
{
    delete plugin;
}

ARKHEON_ASTLIB_API const char* get_plugin_signature()
{
    return "ARKHEON_PLUGIN_V1";
}

} // extern "C"
