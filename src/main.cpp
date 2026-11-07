#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>

using namespace geode::prelude;

// Global flag to prevent recursive updates and unwanted side effects during trajectory simulation
static bool s_isSimulatingTrajectory = false;

// Custom node for rendering the trajectory segments
class TrajectoryDrawNode : public cocos2d::CCDrawNode {
public:
    static TrajectoryDrawNode* create() {
        auto ret = new TrajectoryDrawNode();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

static TrajectoryDrawNode* s_trajectoryNode = nullptr;

class $modify(TrajectoryPlayLayer, PlayLayer) {
    struct Fields {
        Ref<PlayerObject> m_simPlayer = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        s_trajectoryNode = TrajectoryDrawNode::create();
        s_trajectoryNode->setZOrder(9999);
        this->m_objectLayer->addChild(s_trajectoryNode);

        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!s_trajectoryNode) return;

        bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (!enabled || !m_player1 || m_player1->m_isDead || s_isSimulatingTrajectory) {
            s_trajectoryNode->clear();
            return;
        }

        s_trajectoryNode->clear();

        // Simulate both paths:
        // 1. Jump path (holding button): Blue by default
        // 2. Fall path (released button): Red by default
        simulateAndDrawPath(m_player1, true);
        simulateAndDrawPath(m_player1, false);

        // Dual mode support
        if (m_gameState.m_isDualMode && m_player2 && !m_player2->m_isDead) {
            simulateAndDrawPath(m_player2, true);
            simulateAndDrawPath(m_player2, false);
        }
    }

    void simulateAndDrawPath(PlayerObject* realPlayer, bool holdJump) {
        if (!realPlayer) return;

        int ticks = Mod::get()->getSettingValue<int64_t>("trajectory-length");
        float thickness = static_cast<float>(Mod::get()->getSettingValue<double>("line-thickness"));
        ccColor3B rawColor = holdJump ? 
            Mod::get()->getSettingValue<ccColor3B>("color-jump") : 
            Mod::get()->getSettingValue<ccColor3B>("color-fall");

        ccColor4F drawColor = ccc4FFromccc3B(rawColor);
        drawColor.a = 0.85f;

        // Lazy-init simulation ghost player if needed
        if (!m_fields->m_simPlayer) {
            m_fields->m_simPlayer = PlayerObject::create(1, 1, this, this->m_objectLayer, false);
            if (m_fields->m_simPlayer) {
                m_fields->m_simPlayer->setVisible(false);
                m_fields->m_simPlayer->retain();
            }
        }

        auto sim = m_fields->m_simPlayer;
        if (!sim) return;

        s_isSimulatingTrajectory = true;

        // Clone physics state from the real player
        sim->setPosition(realPlayer->getPosition());
        sim->m_yAccel = realPlayer->m_yAccel;
        sim->m_xAccel = realPlayer->m_xAccel;
        sim->m_playerSpeed = realPlayer->m_playerSpeed;
        sim->m_isUpsideDown = realPlayer->m_isUpsideDown;
        sim->m_isShip = realPlayer->m_isShip;
        sim->m_isBird = realPlayer->m_isBird;
        sim->m_isDart = realPlayer->m_isDart;
        sim->m_isRobot = realPlayer->m_isRobot;
        sim->m_isSpider = realPlayer->m_isSpider;
        sim->m_isSwing = realPlayer->m_isSwing;
        sim->m_isOnGround = realPlayer->m_isOnGround;
        sim->m_vehicleSize = realPlayer->m_vehicleSize;
        sim->m_gravity = realPlayer->m_gravity;
        sim->m_isGoingLeft = realPlayer->m_isGoingLeft;
        sim->m_isDead = false;

        // Simulate press or release
        if (holdJump) {
            sim->pushButton(PlayerButton::Jump);
        } else {
            sim->releaseButton(PlayerButton::Jump);
        }

        const float simDt = 1.0f / 240.0f; // High accuracy 240Hz simulation tick
        CCPoint prevPos = sim->getPosition();

        for (int i = 0; i < ticks; ++i) {
            sim->update(simDt);

            CCPoint currentPos = sim->getPosition();
            s_trajectoryNode->drawSegment(prevPos, currentPos, thickness, drawColor);
            prevPos = currentPos;

            if (sim->m_isDead) break;
        }

        // Release simulation button state
        sim->releaseButton(PlayerButton::Jump);

        s_isSimulatingTrajectory = false;
    }

    void onQuit() {
        if (s_trajectoryNode) {
            s_trajectoryNode = nullptr;
        }
        PlayLayer::onQuit();
    }
};

// Suppress dead state triggering and effects during simulation
class $modify(TrajectoryPlayerObject, PlayerObject) {
    void playDeathEffect() {
        if (s_isSimulatingTrajectory) return;
        PlayerObject::playDeathEffect();
    }

    void playSpawnEffect() {
        if (s_isSimulatingTrajectory) return;
        PlayerObject::playSpawnEffect();
    }
};
