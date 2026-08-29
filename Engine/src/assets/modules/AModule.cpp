#include "AModule.h"

namespace Rapture {

AModule::AModule(std::unique_ptr<SerialDocument> document) : m_document(std::move(document)) {}

const TypeInfo &AModule::staticType()
{
    static const TypeInfo type("AModule", &Asset::staticType());
    return type;
}

const TypeInfo &AModule::type() const
{
    return staticType();
}

std::vector<uint8_t> AModule::serialize() const
{
    std::string text = m_document->toText();
    return std::vector<uint8_t>(text.begin(), text.end());
}

} // namespace Rapture
