/**
 * browser_test_runner.cpp - Comprehensive Browser Test Suite
 * 
 * Tests following WebKit/Firefox patterns:
 * - HTML Parsing Tests
 * - CSS Cascade Tests
 * - JavaScript Execution Tests
 * - Layout/Rendering Tests
 * - Integration Tests
 * 
 * Modeled after:
 * - WebKit LayoutTests: https://webkit.org/testing/
 * - Firefox web-platform-tests: https://wpt.fyi/
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <iomanip>
#include <filesystem>

// WebCore includes
#include "html_parser.hpp"
#include "dom.hpp"
#include "css/css_engine.hpp"

// ZebraScript includes
#include "zeprascript/runtime/vm.hpp"

namespace fs = std::filesystem;

using namespace Zepra::WebCore;

// =============================================================================
// Test Framework
// =============================================================================

struct TestResult {
    std::string name;
    std::string category;
    bool passed;
    std::string message;
    double duration_ms;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }
    
    void registerTest(const std::string& category, const std::string& name,
                      std::function<bool(std::string&)> testFn) {
        tests_.push_back({category, name, testFn});
    }
    
    void runAll() {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║         ZEPRABROWSER COMPREHENSIVE TEST SUITE                ║\n";
        std::cout << "║     Following WebKit/Firefox Testing Patterns                ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
        
        std::string currentCategory;
        int passed = 0, failed = 0, skipped = 0;
        
        for (const auto& test : tests_) {
            if (test.category != currentCategory) {
                currentCategory = test.category;
                std::cout << "\n┌─────────────────────────────────────────────────────────────┐\n";
                std::cout << "│ " << std::left << std::setw(60) << currentCategory << "│\n";
                std::cout << "└─────────────────────────────────────────────────────────────┘\n";
            }
            
            std::string message;
            auto start = std::chrono::high_resolution_clock::now();
            
            bool result = false;
            try {
                result = test.testFn(message);
            } catch (const std::exception& e) {
                message = std::string("Exception: ") + e.what();
                result = false;
            } catch (...) {
                message = "Unknown exception";
                result = false;
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double, std::milli>(end - start).count();
            
            TestResult res{test.name, test.category, result, message, duration};
            results_.push_back(res);
            
            if (result) {
                passed++;
                std::cout << "  ✓ " << test.name;
            } else if (message == "SKIP") {
                skipped++;
                std::cout << "  ○ " << test.name << " [SKIPPED]";
            } else {
                failed++;
                std::cout << "  ✗ " << test.name;
                if (!message.empty()) {
                    std::cout << " - " << message;
                }
            }
            std::cout << " (" << std::fixed << std::setprecision(1) << duration << "ms)\n";
        }
        
        // Summary
        std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
        std::cout << "║                        TEST SUMMARY                          ║\n";
        std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
        std::cout << "║  Total:   " << std::setw(5) << tests_.size() << "                                             ║\n";
        std::cout << "║  Passed:  " << std::setw(5) << passed << "  ████████████████████████████████████    ║\n";
        std::cout << "║  Failed:  " << std::setw(5) << failed << "                                             ║\n";
        std::cout << "║  Skipped: " << std::setw(5) << skipped << "                                             ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
        
        if (failed == 0) {
            std::cout << "\n  ★ ALL TESTS PASSED! ★\n\n";
        } else {
            std::cout << "\n  ⚠ SOME TESTS FAILED\n\n";
        }
        
        totalPassed_ = passed;
        totalFailed_ = failed;
    }
    
    int exitCode() const { return totalFailed_ > 0 ? 1 : 0; }
    
private:
    struct Test {
        std::string category;
        std::string name;
        std::function<bool(std::string&)> testFn;
    };
    
    std::vector<Test> tests_;
    std::vector<TestResult> results_;
    int totalPassed_ = 0;
    int totalFailed_ = 0;
};

#define TEST_CASE(category, name) \
    static bool test_##name(std::string& msg); \
    static struct Register_##name { \
        Register_##name() { \
            TestRunner::instance().registerTest(category, #name, test_##name); \
        } \
    } register_##name; \
    static bool test_##name(std::string& msg)

#define EXPECT_TRUE(cond) if (!(cond)) { msg = "Expected true: " #cond; return false; }
#define EXPECT_FALSE(cond) if (cond) { msg = "Expected false: " #cond; return false; }
#define EXPECT_EQ(a, b) if ((a) != (b)) { msg = "Expected " #a " == " #b; return false; }
#define EXPECT_NE(a, b) if ((a) == (b)) { msg = "Expected " #a " != " #b; return false; }
#define SKIP_TEST() { msg = "SKIP"; return false; }

// =============================================================================
// HTML PARSING TESTS
// Following WebKit pattern: test DOM tree structure after parsing
// =============================================================================

TEST_CASE("HTML Parsing", HTMLParser_BasicDocument) {
    HTMLParser parser;
    auto doc = parser.parse("<html><head><title>Test</title></head><body><p>Hello</p></body></html>");
    
    EXPECT_TRUE(doc != nullptr);
    EXPECT_TRUE(doc->documentElement() != nullptr);
    EXPECT_TRUE(doc->body() != nullptr);
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_NestedElements) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html>
            <body>
                <div id="outer">
                    <div id="inner">
                        <span>Text</span>
                    </div>
                </div>
            </body>
        </html>
    )");
    
    EXPECT_TRUE(doc != nullptr);
    
    // Find outer div
    auto outer = doc->getElementById("outer");
    EXPECT_TRUE(outer != nullptr);
    
    // Find inner div
    auto inner = doc->getElementById("inner");
    EXPECT_TRUE(inner != nullptr);
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_Attributes) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html><body>
            <div id="test" class="container main" data-value="123"></div>
        </body></html>
    )");
    
    auto div = doc->getElementById("test");
    EXPECT_TRUE(div != nullptr);
    EXPECT_EQ(div->getAttribute("id"), "test");
    EXPECT_EQ(div->getAttribute("class"), "container main");
    EXPECT_EQ(div->getAttribute("data-value"), "123");
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_TextNodes) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><p>Hello World</p></body></html>");
    
    EXPECT_TRUE(doc->body() != nullptr);
    
    // Find p element
    bool foundText = false;
    for (size_t i = 0; i < doc->body()->childNodes().size(); i++) {
        auto* child = doc->body()->childNodes()[i].get();
        if (auto* el = dynamic_cast<DOMElement*>(child)) {
            if (el->tagName() == "P" || el->tagName() == "p") {
                // Check for text content
                for (size_t j = 0; j < el->childNodes().size(); j++) {
                    if (auto* text = dynamic_cast<DOMText*>(el->childNodes()[j].get())) {
                        if (text->data().find("Hello") != std::string::npos) {
                            foundText = true;
                        }
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundText);
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_SelfClosingTags) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html><body>
            <img src="test.png" alt="Test Image" />
            <br />
            <input type="text" />
        </body></html>
    )");
    
    EXPECT_TRUE(doc != nullptr);
    EXPECT_TRUE(doc->body() != nullptr);
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_Comments) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html>
            <!-- This is a comment -->
            <body>
                <p>Content</p>
                <!-- Another comment -->
            </body>
        </html>
    )");
    
    EXPECT_TRUE(doc != nullptr);
    
    return true;
}

TEST_CASE("HTML Parsing", HTMLParser_SpecialCharacters) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html><body>
            <p>&amp; &lt; &gt; &quot; &apos;</p>
        </body></html>
    )");
    
    EXPECT_TRUE(doc != nullptr);
    
    return true;
}

// =============================================================================
// CSS SPECIFICITY TESTS
// Following Firefox pattern: test cascade order and specificity calculation
// =============================================================================

TEST_CASE("CSS Specificity", Specificity_ElementSelector) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity("div");
    
    // Element selector: (0, 0, 1)
    EXPECT_EQ(spec.a, 0);
    EXPECT_EQ(spec.b, 0); 
    EXPECT_EQ(spec.c, 1);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_ClassSelector) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity(".myclass");
    
    // Class selector: (0, 1, 0)
    EXPECT_EQ(spec.a, 0);
    EXPECT_EQ(spec.b, 1);
    EXPECT_EQ(spec.c, 0);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_IDSelector) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity("#myid");
    
    // ID selector: (1, 0, 0)
    EXPECT_EQ(spec.a, 1);
    EXPECT_EQ(spec.b, 0);
    EXPECT_EQ(spec.c, 0);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_Combined) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity("div.myclass#myid");
    
    // Combined: (1, 1, 1)
    EXPECT_EQ(spec.a, 1);
    EXPECT_EQ(spec.b, 1);
    EXPECT_EQ(spec.c, 1);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_MultipleClasses) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity(".a.b.c");
    
    // Three classes: (0, 3, 0)
    EXPECT_EQ(spec.a, 0);
    EXPECT_EQ(spec.b, 3);
    EXPECT_EQ(spec.c, 0);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_MultipleIDs) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity("#a#b");
    
    // Two IDs: (2, 0, 0)
    EXPECT_EQ(spec.a, 2);
    EXPECT_EQ(spec.b, 0);
    EXPECT_EQ(spec.c, 0);
    
    return true;
}

TEST_CASE("CSS Specificity", Specificity_ComplexSelector) {
    CSSCascade cascade;
    auto spec = cascade.calculateSpecificity("ul#nav li.active a");
    
    // ul#nav: 1 ID + 1 element = (1, 0, 1)
    // li.active: 1 class + 1 element = (0, 1, 1)
    // a: 1 element = (0, 0, 1)
    // Total: (1, 1, 3)
    EXPECT_EQ(spec.a, 1);
    EXPECT_EQ(spec.b, 1);
    EXPECT_EQ(spec.c, 3);
    
    return true;
}

// =============================================================================
// CSS CASCADE ORDERING TESTS
// =============================================================================

TEST_CASE("CSS Cascade", CascadeOrder_IDBeatsClass) {
    MatchedRule idRule;
    idRule.origin = StyleOrigin::Author;
    idRule.specificity = {1, 0, 0};  // ID
    idRule.order = 0;
    
    MatchedRule classRule;
    classRule.origin = StyleOrigin::Author;
    classRule.specificity = {0, 1, 0};  // Class
    classRule.order = 1;
    
    // ID should win
    EXPECT_TRUE(classRule < idRule);
    
    return true;
}

TEST_CASE("CSS Cascade", CascadeOrder_ClassBeatsElement) {
    MatchedRule classRule;
    classRule.origin = StyleOrigin::Author;
    classRule.specificity = {0, 1, 0};
    classRule.order = 0;
    
    MatchedRule elementRule;
    elementRule.origin = StyleOrigin::Author;
    elementRule.specificity = {0, 0, 1};
    elementRule.order = 1;
    
    EXPECT_TRUE(elementRule < classRule);
    
    return true;
}

TEST_CASE("CSS Cascade", CascadeOrder_LaterWinsWithSameSpecificity) {
    MatchedRule early;
    early.origin = StyleOrigin::Author;
    early.specificity = {0, 1, 0};
    early.order = 0;
    
    MatchedRule later;
    later.origin = StyleOrigin::Author;
    later.specificity = {0, 1, 0};
    later.order = 1;
    
    // Later declaration wins
    EXPECT_TRUE(early < later);
    
    return true;
}

TEST_CASE("CSS Cascade", CascadeOrder_AuthorBeatsUserAgent) {
    MatchedRule uaRule;
    uaRule.origin = StyleOrigin::UserAgent;
    uaRule.specificity = {0, 0, 1};
    uaRule.order = 0;
    
    MatchedRule authorRule;
    authorRule.origin = StyleOrigin::Author;
    authorRule.specificity = {0, 0, 1};
    authorRule.order = 1;
    
    EXPECT_TRUE(uaRule < authorRule);
    
    return true;
}

// =============================================================================
// CSS SELECTOR MATCHING TESTS
// =============================================================================

TEST_CASE("CSS Matching", SelectorMatch_TagName) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div id='test'></div></body></html>");
    
    auto div = doc->getElementById("test");
    EXPECT_TRUE(div != nullptr);
    
    CSSCascade cascade;
    EXPECT_TRUE(cascade.selectorMatches(div, "div"));
    EXPECT_FALSE(cascade.selectorMatches(div, "span"));
    
    return true;
}

TEST_CASE("CSS Matching", SelectorMatch_ID) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div id='myid'></div></body></html>");
    
    auto div = doc->getElementById("myid");
    EXPECT_TRUE(div != nullptr);
    
    CSSCascade cascade;
    EXPECT_TRUE(cascade.selectorMatches(div, "#myid"));
    EXPECT_FALSE(cascade.selectorMatches(div, "#otherid"));
    
    return true;
}

TEST_CASE("CSS Matching", SelectorMatch_Class) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div id='test' class='foo bar'></div></body></html>");
    
    auto div = doc->getElementById("test");
    EXPECT_TRUE(div != nullptr);
    
    CSSCascade cascade;
    EXPECT_TRUE(cascade.selectorMatches(div, ".foo"));
    EXPECT_TRUE(cascade.selectorMatches(div, ".bar"));
    EXPECT_FALSE(cascade.selectorMatches(div, ".baz"));
    
    return true;
}

TEST_CASE("CSS Matching", SelectorMatch_Combined) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div id='test' class='myclass'></div></body></html>");
    
    auto div = doc->getElementById("test");
    EXPECT_TRUE(div != nullptr);
    
    CSSCascade cascade;
    EXPECT_TRUE(cascade.selectorMatches(div, "div.myclass"));
    EXPECT_TRUE(cascade.selectorMatches(div, "div#test"));
    EXPECT_TRUE(cascade.selectorMatches(div, "#test.myclass"));
    
    return true;
}

// =============================================================================
// CSS INHERITANCE TESTS
// =============================================================================

TEST_CASE("CSS Inheritance", Inheritance_Color) {
    CSSComputedStyle parent;
    parent.color = CSSColor::fromRGB(255, 0, 0);
    
    CSSComputedStyle child = CSSComputedStyle::inherit(parent);
    
    EXPECT_EQ(child.color.r, 255);
    EXPECT_EQ(child.color.g, 0);
    EXPECT_EQ(child.color.b, 0);
    
    return true;
}

TEST_CASE("CSS Inheritance", Inheritance_FontFamily) {
    CSSComputedStyle parent;
    parent.fontFamily = "Arial";
    
    CSSComputedStyle child = CSSComputedStyle::inherit(parent);
    
    EXPECT_EQ(child.fontFamily, "Arial");
    
    return true;
}

TEST_CASE("CSS Inheritance", Inheritance_FontSize) {
    CSSComputedStyle parent;
    parent.fontSize = 24.0f;
    
    CSSComputedStyle child = CSSComputedStyle::inherit(parent);
    
    EXPECT_EQ(child.fontSize, 24.0f);
    
    return true;
}

TEST_CASE("CSS Inheritance", NoInheritance_Display) {
    CSSComputedStyle parent;
    parent.display = DisplayValue::Flex;
    
    CSSComputedStyle child = CSSComputedStyle::inherit(parent);
    
    // Display should NOT inherit - child should get default Block
    EXPECT_EQ(child.display, DisplayValue::Block);
    
    return true;
}

TEST_CASE("CSS Inheritance", NoInheritance_Margin) {
    CSSComputedStyle parent;
    parent.marginTop = 20.0f;
    parent.marginLeft = 10.0f;
    
    CSSComputedStyle child = CSSComputedStyle::inherit(parent);
    
    // Margins should NOT inherit
    EXPECT_EQ(child.marginTop, 0.0f);
    EXPECT_EQ(child.marginLeft, 0.0f);
    
    return true;
}

// =============================================================================
// CSS VALUE PARSING TESTS
// =============================================================================

TEST_CASE("CSS Values", CSSLength_Pixels) {
    auto len = CSSLength::parse("16px");
    
    EXPECT_EQ(len.value, 16.0f);
    EXPECT_EQ(len.unit, CSSLength::Unit::Px);
    
    return true;
}

TEST_CASE("CSS Values", CSSLength_Em) {
    auto len = CSSLength::parse("1.5em");
    
    EXPECT_EQ(len.value, 1.5f);
    EXPECT_EQ(len.unit, CSSLength::Unit::Em);
    
    return true;
}

TEST_CASE("CSS Values", CSSLength_Rem) {
    auto len = CSSLength::parse("2rem");
    
    EXPECT_EQ(len.value, 2.0f);
    EXPECT_EQ(len.unit, CSSLength::Unit::Rem);
    
    return true;
}

TEST_CASE("CSS Values", CSSLength_Percent) {
    auto len = CSSLength::parse("50%");
    
    EXPECT_EQ(len.value, 50.0f);
    EXPECT_EQ(len.unit, CSSLength::Unit::Percent);
    
    return true;
}

TEST_CASE("CSS Values", CSSLength_Auto) {
    auto len = CSSLength::parse("auto");
    
    EXPECT_TRUE(len.isAuto());
    
    return true;
}

TEST_CASE("CSS Values", CSSColor_Hex6) {
    auto color = CSSColor::fromHex("#ff0000");
    
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
    
    return true;
}

TEST_CASE("CSS Values", CSSColor_Hex3) {
    auto color = CSSColor::fromHex("#f00");
    
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
    
    return true;
}

TEST_CASE("CSS Values", CSSColor_Named) {
    auto black = CSSColor::parse("black");
    EXPECT_EQ(black.r, 0);
    EXPECT_EQ(black.g, 0);
    EXPECT_EQ(black.b, 0);
    
    auto white = CSSColor::parse("white");
    EXPECT_EQ(white.r, 255);
    EXPECT_EQ(white.g, 255);
    EXPECT_EQ(white.b, 255);
    
    return true;
}

TEST_CASE("CSS Values", CSSColor_RGB) {
    auto color = CSSColor::parse("rgb(128, 64, 255)");
    
    EXPECT_EQ(color.r, 128);
    EXPECT_EQ(color.g, 64);
    EXPECT_EQ(color.b, 255);
    
    return true;
}

// =============================================================================
// JAVASCRIPT EXECUTION TESTS
// Following WebKit pattern: test script execution results
// =============================================================================

TEST_CASE("JavaScript", JS_BasicArithmetic) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute("1 + 2 * 3");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 7);
    
    return true;
}

TEST_CASE("JavaScript", JS_Variables) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute("let x = 10; let y = 20; x + y");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 30);
    
    return true;
}

TEST_CASE("JavaScript", JS_Strings) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute("'hello' + ' ' + 'world'");
    
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.asString(), "hello world");
    
    return true;
}

TEST_CASE("JavaScript", JS_Functions) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        function add(a, b) {
            return a + b;
        }
        add(3, 4);
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 7);
    
    return true;
}

TEST_CASE("JavaScript", JS_ArrowFunctions) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        const multiply = (a, b) => a * b;
        multiply(5, 6);
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 30);
    
    return true;
}

TEST_CASE("JavaScript", JS_Objects) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        let obj = { x: 10, y: 20 };
        obj.x + obj.y;
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 30);
    
    return true;
}

TEST_CASE("JavaScript", JS_Arrays) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        let arr = [1, 2, 3, 4, 5];
        arr.length;
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 5);
    
    return true;
}

TEST_CASE("JavaScript", JS_ArrayMethods) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        let arr = [1, 2, 3, 4, 5];
        arr.map(x => x * 2).reduce((a, b) => a + b, 0);
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 30);  // 2+4+6+8+10 = 30
    
    return true;
}

TEST_CASE("JavaScript", JS_Conditionals) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        let x = 10;
        if (x > 5) {
            x = x * 2;
        }
        x;
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 20);
    
    return true;
}

TEST_CASE("JavaScript", JS_Loops) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        let sum = 0;
        for (let i = 1; i <= 10; i++) {
            sum += i;
        }
        sum;
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 55);  // 1+2+...+10 = 55
    
    return true;
}

TEST_CASE("JavaScript", JS_Classes) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        class Point {
            constructor(x, y) {
                this.x = x;
                this.y = y;
            }
            sum() {
                return this.x + this.y;
            }
        }
        let p = new Point(3, 4);
        p.sum();
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 7);
    
    return true;
}

TEST_CASE("JavaScript", JS_Closures) {
    Zepra::Runtime::VM vm;
    auto result = vm.execute(R"(
        function makeCounter() {
            let count = 0;
            return function() {
                count++;
                return count;
            };
        }
        let counter = makeCounter();
        counter();
        counter();
        counter();
    )");
    
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(result.asNumber(), 3);
    
    return true;
}

// =============================================================================
// LAYOUT TESTS
// Following WebKit LayoutTests pattern
// =============================================================================

TEST_CASE("Layout", Layout_BlockDisplay) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div style='display:block'>Block</div></body></html>");
    
    EXPECT_TRUE(doc != nullptr);
    EXPECT_TRUE(doc->body() != nullptr);
    
    // Div should be block by default
    return true;
}

TEST_CASE("Layout", Layout_InlineDisplay) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><span>Inline</span></body></html>");
    
    EXPECT_TRUE(doc != nullptr);
    
    // Span should be inline by default
    return true;
}

TEST_CASE("Layout", Layout_DisplayNone) {
    HTMLParser parser;
    auto doc = parser.parse("<html><body><div id='hidden' style='display:none'>Hidden</div></body></html>");
    
    auto hidden = doc->getElementById("hidden");
    EXPECT_TRUE(hidden != nullptr);
    
    // Element exists but shouldn't be rendered
    return true;
}

// =============================================================================
// INTEGRATION TESTS
// Full page rendering tests
// =============================================================================

TEST_CASE("Integration", Integration_FullPage) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Test Page</title>
            <style>
                body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
                .header { font-size: 24px; color: #333; }
                .content { color: #666; }
                #main { background-color: #f5f5f5; padding: 10px; }
            </style>
        </head>
        <body>
            <h1 class="header">Welcome</h1>
            <div id="main">
                <p class="content">Hello World</p>
            </div>
        </body>
        </html>
    )");
    
    EXPECT_TRUE(doc != nullptr);
    EXPECT_TRUE(doc->documentElement() != nullptr);
    EXPECT_TRUE(doc->body() != nullptr);
    
    auto main = doc->getElementById("main");
    EXPECT_TRUE(main != nullptr);
    
    return true;
}

TEST_CASE("Integration", Integration_InlineStyles) {
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html><body>
            <div id="styled" style="color: red; font-size: 16px; margin: 10px;">
                Inline styled content
            </div>
        </body></html>
    )");
    
    auto styled = doc->getElementById("styled");
    EXPECT_TRUE(styled != nullptr);
    
    std::string style = styled->getAttribute("style");
    EXPECT_TRUE(style.find("color") != std::string::npos);
    EXPECT_TRUE(style.find("font-size") != std::string::npos);
    
    return true;
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    TestRunner::instance().runAll();
    return TestRunner::instance().exitCode();
}
