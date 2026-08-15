#include "ComponentEditors.h"

#include "components/Components.h"
#include "layers/panels/components/header_layouts.h"
#include "logging/Log.h"
#include "renderer/SceneRenderData.h"
#include "scenes/Scene.h"
#include "scenes/instances/Environment.h"

#include <algorithm>

#include <components/checkbox.h>
#include <components/common.h>
#include <components/table.h>

#include <functional>
#include <glm/glm.hpp>
#include <modules/color.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

template <typename T>
static T *s_instanceAs(Rapture::Scene *scene, const Rapture::ecs::EntityAccessor &entity)
{
    if (!entity.isValid() || scene == nullptr) {
        return nullptr;
    }

    Rapture::SceneObject *instance = scene->instanceFor(entity.getEntity());
    return instance != nullptr ? instance->as<T>() : nullptr;
}

Amethyst::Dropdown *ComponentEditorBase::rowMobility(Amethyst::TableScope &t, Rapture::Mobility current,
                                                     const std::function<void(Rapture::Mobility)> &onSelect)
{
    return rowDropdown(t, "Mobility", Rapture::mobilityToString(current), {"Static", "Dynamic"}, [onSelect](int index) {
        if (onSelect) {
            onSelect(static_cast<Rapture::Mobility>(index));
        }
    });
}

void Node3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowVec3(t, "Translation", m_values[0], 0.1, -100000.0, 100000.0, [this]() { apply(0); });
        rowVec3(t, "Rotation", m_values[1], 0.5, -360.0, 360.0, [this]() { apply(1); });
        rowVec3(t, "Scale", m_values[2], 0.01, -1000.0, 1000.0, [this]() { apply(2); });
    });
}

void Node3DEditor::apply(int row)
{
    if (m_node == nullptr) {
        return;
    }
    glm::vec3 v(static_cast<float>(m_values[row][0]), static_cast<float>(m_values[row][1]), static_cast<float>(m_values[row][2]));
    if (row == 0) {
        m_node->setPosition(v);
    } else if (row == 1) {
        m_node->setRotation(v);
    } else {
        m_node->setScale(v);
    }
}

void Node3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Node3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    glm::vec3 t = m_node->position();
    glm::vec3 r = m_node->rotation();
    glm::vec3 s = m_node->scale();
    m_values[0][0] = t.x;
    m_values[0][1] = t.y;
    m_values[0][2] = t.z;
    m_values[1][0] = r.x;
    m_values[1][1] = r.y;
    m_values[1][2] = r.z;
    m_values[2][0] = s.x;
    m_values[2][1] = s.y;
    m_values[2][2] = s.z;
}

void Light3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowColor(t, "Color", m_colorField, m_color, [this](const glm::vec3 &c) {
            if (m_node != nullptr) {
                m_node->setColor(c);
            }
        });
        rowSlider(t, "Intensity", &m_intensity, 0.0f, 100.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setIntensity(v);
            }
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setActive(b);
            }
        });
        rowCheckbox(t, "Casts Shadow", &m_castsShadow, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setCastsShadow(b);
            }
        });
        rowCheckbox(t, "Use Temperature", &m_usesTemperature, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setUsesTemperature(b);
            }
        });
        rowSlider(
            t, "Temperature", &m_temperature, 1000.0f, 40000.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setTemperature(v);
                }
            },
            "%.1f K");
    });
}

void Light3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::Light3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Light3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }

    m_color = m_node->color();
    m_intensity = m_node->intensity();
    m_castsShadow = m_node->castsShadow();
    m_usesTemperature = m_node->usesTemperature();
    m_temperature = m_node->temperature();
    m_isActive = m_node->isActive();

    if (m_colorField) {
        m_colorField->setColor3(Amethyst::Color3(m_color.x, m_color.y, m_color.z));
    }

    if (previous != m_node && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(m_node->mobility()));
    }
}

void DirectionalLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Atmosphere Sun", &m_atmosphereSun, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setAtmosphereSun(b);
            }
        });
    });
}

void DirectionalLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::DirectionalLight3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_atmosphereSun = m_node->isAtmosphereSun();
}

void PointLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
    });
}

void PointLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::PointLight3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
}

void SpotLight3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Range", &m_range, 0.0f, 1000.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setRange(v);
            }
        });
        rowSlider(
            t, "Inner Cone", &m_innerConeAngle, 0.0f, 89.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setInnerConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
        rowSlider(
            t, "Outer Cone", &m_outerConeAngle, 0.0f, 90.0f,
            [this](float v) {
                if (m_node != nullptr) {
                    m_node->setOuterConeAngle(glm::radians(v));
                }
            },
            "%.1f deg");
    });
}

void SpotLight3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::SpotLight3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_range = m_node->range();
    m_innerConeAngle = glm::degrees(m_node->innerConeAngle());
    m_outerConeAngle = glm::degrees(m_node->outerConeAngle());
}

using PhysicsBodyKind = PhysicsEditor::PhysicsBodyKind;

static std::string_view s_physicsBodyLabel(PhysicsBodyKind kind)
{
    switch (kind) {
    case PhysicsEditor::PHYSICS_BODY_RIGID:
        return "Rigid Body";
    case PhysicsEditor::PHYSICS_BODY_CHARACTER:
        return "Character Body";
    default:
        return "None";
    }
}

static PhysicsBodyKind s_physicsBodyOf(const Rapture::SceneObject *node)
{
    if (node->component<Rapture::RigidBody3D>() != nullptr) {
        return PhysicsEditor::PHYSICS_BODY_RIGID;
    }
    if (node->component<Rapture::CharacterBody3D>() != nullptr) {
        return PhysicsEditor::PHYSICS_BODY_CHARACTER;
    }
    return PhysicsEditor::PHYSICS_BODY_NONE;
}

static void s_setPhysicsBody(Rapture::SceneObject *node, PhysicsBodyKind kind)
{
    // adding the kind it already has would attach a second body of that kind
    if (s_physicsBodyOf(node) == kind) {
        return;
    }

    if (kind != PhysicsEditor::PHYSICS_BODY_RIGID) {
        node->removeComponent<Rapture::RigidBody3D>();
    }
    if (kind != PhysicsEditor::PHYSICS_BODY_CHARACTER) {
        node->removeComponent<Rapture::CharacterBody3D>();
    }

    if (kind == PhysicsEditor::PHYSICS_BODY_RIGID) {
        node->addComponent<Rapture::RigidBody3D>();
    } else if (kind == PhysicsEditor::PHYSICS_BODY_CHARACTER) {
        node->addComponent<Rapture::CharacterBody3D>();
    }
}

void Mesh3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowAssetPicker(t, "Mesh", m_meshPicker, {.types = {Rapture::ASSET_MESH}}, [this](Rapture::AssetHandle handle) {
            if (m_node != nullptr) {
                m_node->setMesh(handle);
            }
        });
        rowAssetPicker(t, "Material", m_materialPicker, {.types = {Rapture::ASSET_MATERIAL_INSTANCE}},
                       [this](Rapture::AssetHandle handle) {
                           if (m_node != nullptr) {
                               m_node->setMaterial(handle);
                           }
                       });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_STATIC, [this](Rapture::Mobility m) {
            if (m_node == nullptr) {
                return;
            }
            m_node->setMobility(m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowCheckbox(t, "Visible", &m_isVisible, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setVisible(b);
            }
        });
        rowCheckbox(t, "Ray Traced", &m_isRayTraced, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setRayTraced(b);
            }
        });
    });
}

void Mesh3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::Mesh3D *previous = m_node;
    m_node = s_instanceAs<Rapture::Mesh3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }

    m_isVisible = m_node->isVisible();
    m_isRayTraced = m_node->isRayTraced();

    if (previous != m_node) {
        if (m_mobilityDropdown != nullptr) {
            m_mobilityDropdown->setText(Rapture::mobilityToString(m_node->mobility()));
        }
        if (m_meshPicker.has_value()) {
            m_meshPicker->setAsset(m_node->mesh());
        }
        if (m_materialPicker.has_value()) {
            m_materialPicker->setAsset(m_node->material());
        }
    }
}

// TODO: add kinematic once the simulation stops writing active bodies back onto their node, which is
// the one direction a kinematic body must not be driven in
static constexpr Rapture::physics::MotionType SELECTABLE_MOTION_TYPES[] = {Rapture::physics::MOTION_STATIC,
                                                                           Rapture::physics::MOTION_DYNAMIC};

static std::vector<std::string> s_shapeOptions()
{
    std::vector<std::string> options;
    for (uint32_t type = 0; type < Rapture::physics::COLLISION_SHAPE_COUNT; ++type) {
        options.emplace_back(Rapture::physics::CollisionShape_toString(static_cast<Rapture::physics::CollisionShapeType>(type)));
    }
    return options;
}

Rapture::RigidBody3D *RigidBody3DEditor::resolveBody(const Rapture::ecs::EntityAccessor &entity) const
{
    Rapture::SceneObject *object = s_instanceAs<Rapture::SceneObject>(m_scene, entity);
    return object != nullptr ? object->component<Rapture::RigidBody3D>() : nullptr;
}

void PhysicsEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    std::vector<std::string> options;
    for (uint32_t kind = 0; kind < PHYSICS_BODY_COUNT; ++kind) {
        options.emplace_back(s_physicsBodyLabel(static_cast<PhysicsBodyKind>(kind)));
    }

    auto *layout = ch.component.addExtension<Amethyst::UIListLayout>();
    layout->fillDirection = Amethyst::FillDirection::FILL_VERTICAL;
    layout->innerPadding = Amethyst::UDim::fromOffset(SECTION_SPACING);

    m_physicsSubSelector = ch.component.add<SegmentedControl>(options, SegmentedSelection::SINGLE);
    m_physicsSubSelector->setBaseProperties({
        .padding = Amethyst::UDim4{.top = Amethyst::UDim::fromOffset(10.0f),
                                   .right = Amethyst::UDim::fromScale(0.05f),
                                   .bottom = Amethyst::UDim::fromOffset(10.0f),
                                   .left = Amethyst::UDim::fromScale(0.05f)},
        .size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, 50.0f),
    });
    m_physicsSubSelector->setBaseStyleProperties({.backgroundTransparency = 1.0f});

    m_physicsSubSelector->onChanged = [this](int32_t index, bool selected) {
        const PhysicsBodyKind kind = static_cast<PhysicsBodyKind>(index);

        // sync selects the option the object already has, which must not be taken for a change
        if (m_owner == nullptr || !selected || s_physicsBodyOf(m_owner) == kind) {
            return;
        }

        s_setPhysicsBody(m_owner, kind);
    };
}

void PhysicsEditor::buildPhysicsSubEditor(PhysicsBodyKind kind)
{
    if (header == nullptr) {
        return;
    }

    if (m_physicsSubHeader != nullptr) {
        header->removeChild(m_physicsSubHeader);
        m_physicsSubHeader = nullptr;
        header->markDirty();
    }
    m_physicsSubEditor.reset();
    m_builtKind = kind;

    switch (kind) {
    case PHYSICS_BODY_RIGID: {
        m_physicsSubEditor = std::make_unique<RigidBody3DEditor>();
        break;
    }
    case PHYSICS_BODY_CHARACTER: {
        m_physicsSubEditor = std::make_unique<CharacterBody3DEditor>();
        break;
    }
    case PHYSICS_BODY_NONE: {
        return;
    }
    default: {
        RP_ERROR("physics body kind {} has no editor", static_cast<uint32_t>(kind));
        return;
    }
    }

    m_physicsSubEditor->setSubject(m_scene, m_entity);

    Amethyst::CollapsibleHeaderScope scope(*header);
    scope.collapsibleHeader(
        {
            .classes = {"component-header"},
            .base = {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT)},
            .style = {.backgroundTransparency = 1.0f},
            .header =
                {
                    .titleStyle = {.fontSize = 13.0f},
                    .headerHeight = HEADER_HEIGHT,
                },
        },
        [this](Amethyst::CollapsibleHeaderScope &nested) {
            m_physicsSubHeader = &nested.component;
            nested.header(componentHeaderLayout(m_physicsSubEditor->title(), m_physicsSubEditor->icon()));
            m_physicsSubEditor->buildBody(nested);
            m_physicsSubHeader->setBaseProperties(
                {.size = Amethyst::UDim2(1.0f, 0.0f, 0.0f, HEADER_HEIGHT + m_physicsSubEditor->bodyHeight())});
        });

    m_physicsSubHeader->onToggled = [this](bool) {
        if (requestRelayout != nullptr) {
            requestRelayout();
        }
    };

    header->markDirty();
}

float PhysicsEditor::bodyHeight() const
{
    if (m_physicsSubSelector == nullptr) {
        return 0.0f;
    }

    float height = m_physicsSubSelector->getBaseProperties().size.offset.y;
    if (m_physicsSubEditor == nullptr || m_physicsSubHeader == nullptr) {
        return height;
    }

    height += HEADER_HEIGHT + SECTION_SPACING;
    if (static_cast<bool>(m_physicsSubHeader->getCollapsibleHeaderProperties().expanded)) {
        height += m_physicsSubEditor->bodyHeight();
    }

    return height;
}

void PhysicsEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_owner = s_instanceAs<Rapture::SceneObject>(m_scene, entity);
    if (m_owner == nullptr) {
        return;
    }

    const PhysicsBodyKind kind = s_physicsBodyOf(m_owner);

    if (m_physicsSubSelector != nullptr) {
        m_physicsSubSelector->select(static_cast<int32_t>(kind));
    }

    if (kind != m_builtKind) {
        buildPhysicsSubEditor(kind);
        if (requestRelayout != nullptr) {
            requestRelayout();
        }
    }

    if (m_physicsSubEditor != nullptr) {
        m_physicsSubEditor->sync();
    }
}

void PhysicsEditor::setSubject(Rapture::Scene *scene, const Rapture::ecs::EntityAccessor &entity)
{
    ComponentEditorBase::setSubject(scene, entity);

    if (m_physicsSubEditor != nullptr) {
        m_physicsSubEditor->setSubject(scene, entity);
    }
}

void RigidBody3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_shapeDropdown = rowDropdown(t, "Shape", Rapture::physics::CollisionShape_toString(Rapture::physics::COLLISION_SHAPE_BOX),
                                      s_shapeOptions(), [this](int index) {
                                          if (m_rigidBody == nullptr) {
                                              return;
                                          }
                                          const auto type = static_cast<Rapture::physics::CollisionShapeType>(index);
                                          m_rigidBody->setShape(Rapture::physics::CollisionShape_ofType(type));
                                          if (m_shapeDropdown != nullptr) {
                                              m_shapeDropdown->setText(std::string(Rapture::physics::CollisionShape_toString(type)));
                                          }
                                      });
        m_motionTypeDropdown =
            rowDropdown(t, "Motion", Rapture::physics::MotionType_toString(Rapture::physics::MOTION_DYNAMIC), {"Static", "Dynamic"},
                        [this](int index) {
                            if (m_rigidBody == nullptr) {
                                return;
                            }
                            const Rapture::physics::MotionType motionType = SELECTABLE_MOTION_TYPES[index];
                            m_rigidBody->setMotionType(motionType);
                            if (m_motionTypeDropdown != nullptr) {
                                m_motionTypeDropdown->setText(std::string(Rapture::physics::MotionType_toString(motionType)));
                            }
                        });
        rowSlider(t, "Friction", &m_friction, 0.0f, 1.0f, [this](float v) {
            if (m_rigidBody != nullptr) {
                m_rigidBody->setFriction(v);
            }
        });
        rowSlider(t, "Restitution", &m_restitution, 0.0f, 1.0f, [this](float v) {
            if (m_rigidBody != nullptr) {
                m_rigidBody->setRestitution(v);
            }
        });
    });
}

void RigidBody3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::RigidBody3D *previous = m_rigidBody;
    m_rigidBody = resolveBody(entity);
    if (m_rigidBody == nullptr) {
        return;
    }

    m_friction = m_rigidBody->friction();
    m_restitution = m_rigidBody->restitution();

    if (previous != m_rigidBody) {
        if (m_shapeDropdown != nullptr) {
            m_shapeDropdown->setText(std::string(
                Rapture::physics::CollisionShape_toString(Rapture::physics::CollisionShape_typeOf(m_rigidBody->shape()))));
        }
        if (m_motionTypeDropdown != nullptr) {
            m_motionTypeDropdown->setText(std::string(Rapture::physics::MotionType_toString(m_rigidBody->motionType())));
        }
    }
}

Rapture::CharacterBody3D *CharacterBody3DEditor::resolveBody(const Rapture::ecs::EntityAccessor &entity) const
{
    Rapture::SceneObject *object = s_instanceAs<Rapture::SceneObject>(m_scene, entity);
    return object != nullptr ? object->component<Rapture::CharacterBody3D>() : nullptr;
}

void CharacterBody3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        m_shapeDropdown = rowDropdown(t, "Shape", Rapture::physics::CollisionShape_toString(Rapture::physics::COLLISION_SHAPE_CAPSULE),
                                      s_shapeOptions(), [this](int index) {
                                          if (m_characterBody == nullptr) {
                                              return;
                                          }
                                          const auto type = static_cast<Rapture::physics::CollisionShapeType>(index);
                                          m_characterBody->setShape(Rapture::physics::CollisionShape_ofType(type));
                                          if (m_shapeDropdown != nullptr) {
                                              m_shapeDropdown->setText(std::string(Rapture::physics::CollisionShape_toString(type)));
                                          }
                                      });
        rowVec3(t, "Shape Offset", m_shapeOffset, 0.01, -100.0, 100.0, [this]() {
            if (m_characterBody != nullptr) {
                m_characterBody->setShapeOffset(glm::vec3(static_cast<float>(m_shapeOffset[0]),
                                                          static_cast<float>(m_shapeOffset[1]),
                                                          static_cast<float>(m_shapeOffset[2])));
            }
        });
        rowSlider(t, "Mass", &m_mass, 1.0f, 500.0f, [this](float v) {
            if (m_characterBody != nullptr) {
                m_characterBody->setMass(v);
            }
        });
        rowSlider(
            t, "Max Slope", &m_maxSlopeAngle, 0.0f, 90.0f,
            [this](float v) {
                if (m_characterBody != nullptr) {
                    m_characterBody->setMaxSlopeAngle(glm::radians(v));
                }
            },
            "%.0f deg");
        rowSlider(t, "Step Up", &m_stepUp, 0.0f, 2.0f, [this](float v) {
            if (m_characterBody != nullptr) {
                m_characterBody->setStepUp(v);
            }
        });
        rowSlider(t, "Step Down", &m_stepDown, 0.0f, 2.0f, [this](float v) {
            if (m_characterBody != nullptr) {
                m_characterBody->setStepDown(v);
            }
        });
        rowSlider(t, "Jump Speed", &m_jumpSpeed, 0.0f, 20.0f, [this](float v) {
            if (m_characterBody != nullptr) {
                m_characterBody->setJumpSpeed(v);
            }
        });
    });
}

void CharacterBody3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    Rapture::CharacterBody3D *previous = m_characterBody;
    m_characterBody = resolveBody(entity);
    if (m_characterBody == nullptr) {
        return;
    }

    const glm::vec3 offset = m_characterBody->shapeOffset();
    m_shapeOffset[0] = offset.x;
    m_shapeOffset[1] = offset.y;
    m_shapeOffset[2] = offset.z;

    m_mass = m_characterBody->mass();
    m_maxSlopeAngle = glm::degrees(m_characterBody->maxSlopeAngle());
    m_stepUp = m_characterBody->stepUp();
    m_stepDown = m_characterBody->stepDown();
    m_jumpSpeed = m_characterBody->jumpSpeed();

    if (previous != m_characterBody && m_shapeDropdown != nullptr) {
        m_shapeDropdown->setText(std::string(
            Rapture::physics::CollisionShape_toString(Rapture::physics::CollisionShape_typeOf(m_characterBody->shape()))));
    }
}

void SpringArm3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowDragFloat(t, "Length", &m_length, 0.05, 0.0, 1000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setLength(static_cast<float>(v));
            }
        });
    });
}

void SpringArm3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::SpringArm3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_length = m_node->length();
}

void Camera3DEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "FOV", &m_fieldOfView, 1.0f, 179.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setFieldOfView(v);
            }
        });
        rowDragFloat(t, "Near Plane", &m_nearPlane, 0.01, 0.001, 10000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setNearPlane(static_cast<float>(v));
            }
        });
        rowDragFloat(t, "Far Plane", &m_farPlane, 1.0, 0.001, 1000000.0, {}, [this](double v) {
            if (m_node != nullptr) {
                m_node->setFarPlane(static_cast<float>(v));
            }
        });
    });
}

void Camera3DEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Camera3D>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }
    m_fieldOfView = m_node->fieldOfView();
    m_nearPlane = m_node->nearPlane();
    m_farPlane = m_node->farPlane();
}

void ShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::ShadowComponent>()) {
                return;
            }
            m_entity.write<Rapture::ShadowComponent>()->isActive = b;
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::ShadowComponent>()) {
                return;
            }
            m_scene->getRenderData()->setShadowMobility(m_entity.getEntity(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
    });
}

void ShadowEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    bool entityChanged = subjectChanged();
    if (!entity.has<Rapture::ShadowComponent>()) {
        return;
    }
    const auto &sc = entity.read<Rapture::ShadowComponent>();
    m_isActive = sc.isActive;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(sc.mobility));
    }
}

void CascadedShadowEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowCheckbox(t, "Active", &m_isActive, [this](bool b) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            m_entity.write<Rapture::CascadedShadowComponent>()->isActive = b;
        });
        m_mobilityDropdown = rowMobility(t, Rapture::MOBILITY_DYNAMIC, [this](Rapture::Mobility m) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            m_scene->getRenderData()->setCascadedShadowMobility(m_entity.getEntity(), m);
            if (m_mobilityDropdown != nullptr) {
                m_mobilityDropdown->setText(Rapture::mobilityToString(m));
            }
        });
        rowSlider(t, "Lambda", &m_lambda, 0.0f, 1.0f, [this](float v) {
            if (!m_entity.isValid() || !m_entity.has<Rapture::CascadedShadowComponent>()) {
                return;
            }
            auto csc = m_entity.write<Rapture::CascadedShadowComponent>();
            csc->lambda = std::clamp(v, 0.0f, 1.0f);
        });
    });
}

void CascadedShadowEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    bool entityChanged = subjectChanged();
    if (!entity.has<Rapture::CascadedShadowComponent>()) {
        return;
    }
    const auto &csc = entity.read<Rapture::CascadedShadowComponent>();
    m_isActive = csc.isActive;
    m_lambda = csc.lambda;

    if (entityChanged && m_mobilityDropdown != nullptr) {
        m_mobilityDropdown->setText(Rapture::mobilityToString(csc.mobility));
    }
}

void EnvironmentEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Sky Intensity", &m_skyIntensity, 0.0f, 10.0f, [this](float v) {
            if (m_node != nullptr) {
                m_node->setSkyIntensity(v);
            }
        });
        rowCheckbox(t, "Sky Enabled", &m_skyboxEnabled, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setSkyboxEnabled(b);
            }
        });
        rowCheckbox(t, "Atmosphere Sky", &m_usesAtmosphereSkybox, [this](bool b) {
            if (m_node != nullptr) {
                m_node->setUsesAtmosphereSkybox(b);
            }
        });
        rowSlider(t, "Time of Day", &m_atmosphere.timeOfDay, 0.0f, 24.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Latitude", &m_atmosphere.latitude, -90.0f, 90.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Longitude", &m_atmosphere.longitude, -180.0f, 180.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Mie", &m_atmosphere.mie, 0.0f, 60.0f, [this](float) { pushAtmosphere(); });
        rowSlider(t, "Mie G", &m_atmosphere.mieG, 0.0f, 0.999f, [this](float) { pushAtmosphere(); });
        rowDragFloat(t, "Sun Intensity", &m_sunIntensity, 0.1, 0.0, 1000.0, {}, [this](double v) {
            m_atmosphere.sunIntensity = static_cast<float>(v);
            pushAtmosphere();
        });
        rowDragFloat(t, "Camera Altitude", &m_cameraAltitude, 1.0, 0.0, 100000.0, {}, [this](double v) {
            m_atmosphere.cameraAltitude = static_cast<float>(v);
            pushAtmosphere();
        });
        rowVec3(t, "Wavelength (nm)", m_wavelengths, 1.0, 380.0, 740.0, [this]() {
            m_atmosphere.rayleigh = glm::vec3(Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[0])),
                                              Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[1])),
                                              Rapture::Environment::rayleighCoefficient(static_cast<float>(m_wavelengths[2])));
            pushAtmosphere();
        });
    });
}

void EnvironmentEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_node = s_instanceAs<Rapture::Environment>(m_scene, entity);
    if (m_node == nullptr) {
        return;
    }

    m_skyIntensity = m_node->skyIntensity();
    m_skyboxEnabled = m_node->isSkyboxEnabled();
    m_usesAtmosphereSkybox = m_node->usesAtmosphereSkybox();

    m_atmosphere = m_node->atmosphere();
    m_sunIntensity = m_atmosphere.sunIntensity;
    m_cameraAltitude = m_atmosphere.cameraAltitude;
    m_wavelengths[0] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.x);
    m_wavelengths[1] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.y);
    m_wavelengths[2] = Rapture::Environment::wavelengthNm(m_atmosphere.rayleigh.z);
}

void EnvironmentEditor::pushAtmosphere()
{
    if (m_node == nullptr) {
        return;
    }
    m_node->atmosphere() = m_atmosphere;
}

void StubEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    ch.textLabel({
        .classes = {"property-label"},
        .base =
            {
                .position = Amethyst::UDim2::fromOffset(4.0f, 4.0f),
                .size = Amethyst::UDim2(1.0f, -8.0f, 0.0f, 28.0f),
            },
        .label = "Not yet implemented",
    });
}

void CameraControllerEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Movement Speed", &m_movementSpeed, 0.1f, 100.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->movementSpeed = v;
            }
        });
        rowSlider(t, "Mouse Sensitivity", &m_mouseSensitivity, 0.01f, 1.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->mouseSensitivity = v;
            }
        });
        rowSlider(t, "Orbit Sensitivity", &m_orbitSensitivity, 0.01f, 2.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->orbitSensitivity = v;
            }
        });
        rowSlider(
            t, "Pan Speed", &m_panSpeed, 0.0001f, 0.01f,
            [this](float v) {
                if (m_controller != nullptr) {
                    m_controller->panSpeed = v;
                }
            },
            "%.4f");
        rowSlider(t, "Zoom Speed", &m_zoomSpeed, 0.01f, 1.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->zoomSpeed = v;
            }
        });
        rowSlider(
            t, "Max Pitch", &m_maxPitch, 0.0f, 89.9f,
            [this](float v) {
                if (m_controller != nullptr) {
                    m_controller->maxPitch = v;
                }
            },
            "%.1f deg");
    });
}

void CameraControllerEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_controller = s_instanceAs<Rapture::CameraController>(m_scene, entity);
    if (m_controller == nullptr) {
        return;
    }

    m_mouseSensitivity = m_controller->mouseSensitivity;
    m_movementSpeed = m_controller->movementSpeed;
    m_orbitSensitivity = m_controller->orbitSensitivity;
    m_panSpeed = m_controller->panSpeed;
    m_zoomSpeed = m_controller->zoomSpeed;
    m_maxPitch = m_controller->maxPitch;
}

void PlayerControllerEditor::buildBody(Amethyst::CollapsibleHeaderScope &ch)
{
    fieldTable(ch, [this](Amethyst::TableScope &t) {
        rowSlider(t, "Movement Speed", &m_movementSpeed, 0.1f, 100.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->movementSpeed = v;
            }
        });
        rowSlider(t, "Mouse Sensitivity", &m_mouseSensitivity, 0.01f, 1.0f, [this](float v) {
            if (m_controller != nullptr) {
                m_controller->mouseSensitivity = v;
            }
        });
        rowSlider(
            t, "Max Pitch", &m_maxPitch, 0.0f, 89.9f,
            [this](float v) {
                if (m_controller != nullptr) {
                    m_controller->maxPitch = v;
                }
            },
            "%.1f deg");
    });
}

void PlayerControllerEditor::sync(const Rapture::ecs::EntityAccessor &entity)
{
    m_controller = s_instanceAs<Rapture::PlayerController>(m_scene, entity);
    if (m_controller == nullptr) {
        return;
    }

    m_movementSpeed = m_controller->movementSpeed;
    m_mouseSensitivity = m_controller->mouseSensitivity;
    m_maxPitch = m_controller->maxPitch;
}
