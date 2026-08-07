#include "ModuleClass.h"

#include "logging/Log.h"
#include "modules/ModuleRegistry.h"

namespace Rapture {

static constexpr std::string_view KEY_CLASS = "class";

const TypeInfo &ModuleClass::staticType()
{
    static const TypeInfo type("ModuleClass", nullptr);
    return type;
}

void ModuleClass::serialize(WriteNode node) const
{
    node.set(KEY_CLASS, type().name);
}

void ModuleClass::deserialize(ReadNode node)
{
    (void)node;
}

std::string_view ModuleClass::readClassName(ReadNode node)
{
    return node.child(KEY_CLASS).asString();
}

std::unique_ptr<ModuleClass> ModuleClass::load(ReadNode node)
{
    std::string_view className = readClassName(node);

    std::unique_ptr<ModuleClass> created = ModuleRegistry::create(className);
    if (created == nullptr) {
        RP_CORE_ERROR("no module class named '{}'", className);
        return nullptr;
    }

    created->deserialize(node);
    return created;
}

std::vector<uint8_t> ModuleClass::toBlob() const
{
    SerialDocument builder;
    serialize(builder.root());

    std::string text = builder.toText();
    if (text.empty()) {
        RP_CORE_ERROR("module document could not be written");
        return {};
    }

    return std::vector<uint8_t>(text.begin(), text.end());
}

std::unique_ptr<ModuleClass> ModuleClass::fromBlob(std::span<const uint8_t> blob)
{
    std::string_view text(reinterpret_cast<const char *>(blob.data()), blob.size());

    SerialDocument document = SerialDocument::parse(text);
    if (!document.rootView().valid()) {
        RP_CORE_ERROR("module blob does not hold a readable document");
        return nullptr;
    }

    return load(document.rootView());
}

} // namespace Rapture
