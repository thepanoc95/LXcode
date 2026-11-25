#include "test_support.hpp"

#include "lxcode/project/pbxproj.hpp"

#include <algorithm>

using namespace lxcode;
using namespace lxcode::test;

int main() {
    const std::string source = R"(// !$*UTF8*$!
{
	archiveVersion = 1;
	classes = {
	};
	objectVersion = 56;
	objects = {

/* Begin PBXProject section */
		A1B2C3 /* Project object */ = {
			isa = PBXProject;
			attributes = {
				LastUpgradeCheck = 1600;
				UnknownVendorKey = preservedValue;
			};
			buildConfigurationList = D4E5F6 /* Build configuration list */;
			compatibilityVersion = "Xcode 14.0";
			mainGroup = A0B0C0;
			targets = (
			);
		};
/* End PBXProject section */
	};
	rootObject = A1B2C3 /* Project object */;
}
)";

    pbx::PbxDocument document;
    auto parsed = document.parse(source);
    LXCHECK_TRUE(parsed);
    LXCHECK_TRUE(document.isParsed());
    LXCHECK_EQ(document.rootObjectId(), std::string("A1B2C3"));
    LXCHECK_EQ(document.isa("A1B2C3"), std::string("PBXProject"));
    LXCHECK_TRUE(!document.objectIdsOfClass("PBXProject").empty());

    const auto* attributes = document.root().find("objects")
                                 ->find("A1B2C3")
                                 ->find("attributes");
    LXCHECK_TRUE(attributes != nullptr);
    LXCHECK_EQ(attributes->stringValue("UnknownVendorKey"), std::string("preservedValue"));

    std::string serialized = document.serialize();
    LXCHECK_TRUE(serialized.find("UnknownVendorKey") != std::string::npos);
    LXCHECK_TRUE(serialized.find("preservedValue") != std::string::npos);
    LXCHECK_TRUE(serialized.find("Project object") != std::string::npos);

    LXCHECK_TRUE(document.setString("A1B2C3", "compatibilityVersion", "Xcode 16.0", true));
    serialized = document.serialize();
    LXCHECK_TRUE(serialized.find("\"Xcode 16.0\"") != std::string::npos);
    LXCHECK_TRUE(serialized.find("UnknownVendorKey") != std::string::npos);

    const auto first = document.makeIdentifier("test");
    const auto second = document.makeIdentifier("test");
    LXCHECK_TRUE(first != second);
    LXCHECK_EQ(first.size(), std::size_t(24));

    auto missing = pbx::PbxDocument();
    auto bad = missing.parse("{ objects = { broken = { ; }; }; }");
    LXCHECK_TRUE(!bad);

    std::cout << "pbxproj tests passed\n";
    return 0;
}
