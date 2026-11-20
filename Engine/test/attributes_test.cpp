#include <rapture/core/named_values/Attributes.h>
#include <rapture/core/serialization/SerialDocument.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Rapture::Attributes;
using Rapture::SerialDocument;
using Rapture::ValueType;

static constexpr std::string_view KEY_ATTRIBUTES = "attributes";

TEST(Attributes, WritingANameNothingHoldsStartsHoldingIt)
{
    Attributes attributes;
    attributes.setFloat("speed", 2.5f);
    attributes.setBool("grounded", true);
    attributes.setInt("lives", 3);
    attributes.setVec3("spawn", glm::vec3(1.0f, 2.0f, 3.0f));

    EXPECT_FLOAT_EQ(attributes.getFloat("speed"), 2.5f);
    EXPECT_TRUE(attributes.getBool("grounded"));
    EXPECT_EQ(attributes.getInt("lives"), 3);
    EXPECT_EQ(attributes.getVec3("spawn"), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(Attributes, OverwritingKeepsOneValuePerName)
{
    Attributes attributes;
    attributes.setFloat("speed", 1.0f);
    attributes.setFloat("speed", 4.0f);

    EXPECT_FLOAT_EQ(attributes.getFloat("speed"), 4.0f);
    EXPECT_EQ(attributes.values().entries().size(), 1u);
}

TEST(Attributes, MissingNameAnswersWithTheFallback)
{
    Attributes attributes;

    EXPECT_FLOAT_EQ(attributes.getFloat("speed", 7.0f), 7.0f);
    EXPECT_EQ(attributes.typeOf("speed"), ValueType::NONE);
    EXPECT_FALSE(attributes.has("speed"));
}

TEST(Attributes, WritingAnotherTypeReplacesTheValue)
{
    Attributes attributes;
    attributes.setFloat("value", 1.0f);
    attributes.setBool("value", true);

    EXPECT_EQ(attributes.typeOf("value"), ValueType::BOOL);
    EXPECT_TRUE(attributes.getBool("value"));
    EXPECT_FLOAT_EQ(attributes.getFloat("value", -1.0f), -1.0f);
    EXPECT_EQ(attributes.values().entries().size(), 1u);
}

TEST(Attributes, RemovingKeepsTheOthers)
{
    Attributes attributes;
    attributes.setFloat("a", 1.0f);
    attributes.setFloat("b", 2.0f);
    attributes.setFloat("c", 3.0f);

    EXPECT_TRUE(attributes.remove("b"));
    EXPECT_FALSE(attributes.remove("b"));

    EXPECT_FALSE(attributes.has("b"));
    EXPECT_FLOAT_EQ(attributes.getFloat("a"), 1.0f);
    EXPECT_FLOAT_EQ(attributes.getFloat("c"), 3.0f);
}

TEST(Attributes, ChangesAreAnnounced)
{
    Attributes attributes;
    std::vector<std::string> changed;
    Rapture::EventConnection connection = attributes.onChanged.connect([&changed](std::string_view name) { changed.emplace_back(name); });

    attributes.setInt("lives", 3);
    attributes.remove("lives");

    EXPECT_EQ(changed, (std::vector<std::string>{"lives", "lives"}));
}

TEST(Attributes, RoundTripsThroughADocument)
{
    Attributes written;
    written.setFloat("speed", 2.5f);
    written.setBool("grounded", true);
    written.setInt("lives", -2);
    written.setVec3("spawn", glm::vec3(1.0f, -2.0f, 3.5f));

    SerialDocument document;
    written.serialize(document.root().addArray(KEY_ATTRIBUTES));
    ASSERT_TRUE(document.freeze());

    Attributes read;
    read.setFloat("stale", 1.0f);
    read.deserialize(document.rootView().child(KEY_ATTRIBUTES));

    EXPECT_FALSE(read.has("stale"));
    EXPECT_FLOAT_EQ(read.getFloat("speed"), 2.5f);
    EXPECT_TRUE(read.getBool("grounded"));
    EXPECT_EQ(read.getInt("lives"), -2);
    EXPECT_EQ(read.getVec3("spawn"), glm::vec3(1.0f, -2.0f, 3.5f));
}
