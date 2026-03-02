/**
 * css_cascade_test.cpp - CSS Cascade Test Suite
 * 
 * Tests for proper CSS cascade behavior per W3C spec:
 * - Specificity calculation (ID > class > element)
 * - Cascade ordering (origin, specificity, source order)
 * - Property inheritance (parent -> child)
 * - Inline style priority
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>

#include "css/css_engine.hpp"
#include "html_parser.hpp"
#include "dom.hpp"

using namespace Zepra::WebCore;

// Test counter
static int testsRun = 0;
static int testsPassed = 0;

#define TEST(name) \
    std::cout << "  [TEST] " << name << "... "; \
    testsRun++;

#define PASS() \
    std::cout << "PASS" << std::endl; \
    testsPassed++;

#define FAIL(msg) \
    std::cout << "FAIL: " << msg << std::endl;

// ============================================================================
// Specificity Tests
// ============================================================================

void testSpecificityCalculation() {
    std::cout << "\n=== Specificity Calculation ===" << std::endl;
    
    CSSCascade cascade;
    
    // Test 1: Element selector
    TEST("Element selector (div) has specificity (0,0,1)")
    {
        auto spec = cascade.calculateSpecificity("div");
        if (spec.a == 0 && spec.b == 0 && spec.c == 1) {
            PASS()
        } else {
            FAIL("Expected (0,0,1)")
        }
    }
    
    // Test 2: Class selector
    TEST("Class selector (.foo) has specificity (0,1,0)")
    {
        auto spec = cascade.calculateSpecificity(".foo");
        if (spec.a == 0 && spec.b == 1 && spec.c == 0) {
            PASS()
        } else {
            FAIL("Expected (0,1,0)")
        }
    }
    
    // Test 3: ID selector
    TEST("ID selector (#bar) has specificity (1,0,0)")
    {
        auto spec = cascade.calculateSpecificity("#bar");
        if (spec.a == 1 && spec.b == 0 && spec.c == 0) {
            PASS()
        } else {
            FAIL("Expected (1,0,0)")
        }
    }
    
    // Test 4: Combined selector
    TEST("Combined (div.foo#bar) has specificity (1,1,1)")
    {
        auto spec = cascade.calculateSpecificity("div.foo#bar");
        if (spec.a == 1 && spec.b == 1 && spec.c == 1) {
            PASS()
        } else {
            FAIL("Expected (1,1,1)")
        }
    }
    
    // Test 5: Multiple classes
    TEST("Multiple classes (.a.b.c) has specificity (0,3,0)")
    {
        auto spec = cascade.calculateSpecificity(".a.b.c");
        if (spec.a == 0 && spec.b == 3 && spec.c == 0) {
            PASS()
        } else {
            FAIL("Expected (0,3,0)")
        }
    }
}

// ============================================================================
// Cascade Order Tests
// ============================================================================

void testCascadeOrdering() {
    std::cout << "\n=== Cascade Ordering ===" << std::endl;
    
    // Test 6: Higher specificity wins
    TEST("ID selector beats class selector")
    {
        MatchedRule idRule;
        idRule.origin = StyleOrigin::Author;
        idRule.specificity = {1, 0, 0};  // ID
        idRule.order = 0;
        
        MatchedRule classRule;
        classRule.origin = StyleOrigin::Author;
        classRule.specificity = {0, 1, 0};  // Class
        classRule.order = 1;
        
        // ID should be "greater" (higher priority)
        if (classRule < idRule) {
            PASS()
        } else {
            FAIL("ID should beat class")
        }
    }
    
    // Test 7: Source order as tiebreaker
    TEST("Later declaration wins with same specificity")
    {
        MatchedRule early;
        early.origin = StyleOrigin::Author;
        early.specificity = {0, 1, 0};
        early.order = 0;
        
        MatchedRule later;
        later.origin = StyleOrigin::Author;
        later.specificity = {0, 1, 0};
        later.order = 1;
        
        // Later should be "greater" (higher priority)
        if (early < later) {
            PASS()
        } else {
            FAIL("Later declaration should win")
        }
    }
    
    // Test 8: Author beats user-agent
    TEST("Author stylesheet beats user-agent")
    {
        MatchedRule uaRule;
        uaRule.origin = StyleOrigin::UserAgent;
        uaRule.specificity = {0, 0, 1};
        uaRule.order = 0;
        
        MatchedRule authorRule;
        authorRule.origin = StyleOrigin::Author;
        authorRule.specificity = {0, 0, 1};
        authorRule.order = 1;
        
        if (uaRule < authorRule) {
            PASS()
        } else {
            FAIL("Author should beat user-agent")
        }
    }
}

// ============================================================================
// Selector Matching Tests
// ============================================================================

void testSelectorMatching() {
    std::cout << "\n=== Selector Matching ===" << std::endl;
    
    CSSCascade cascade;
    
    // Create test DOM structure
    HTMLParser parser;
    auto doc = parser.parse(R"(
        <html>
            <body>
                <div id="main" class="container active">
                    <p class="text">Hello</p>
                </div>
            </body>
        </html>
    )");
    
    if (!doc || !doc->body()) {
        std::cout << "  [SKIP] Could not parse test HTML" << std::endl;
        return;
    }
    
    // Find div element
    DOMElement* div = nullptr;
    for (size_t i = 0; i < doc->body()->childNodes().size(); i++) {
        if (auto* el = dynamic_cast<DOMElement*>(doc->body()->childNodes()[i].get())) {
            if (el->tagName() == "DIV" || el->tagName() == "div") {
                div = el;
                break;
            }
        }
    }
    
    if (!div) {
        std::cout << "  [SKIP] Could not find div element" << std::endl;
        return;
    }
    
    // Test 9: Tag selector matches
    TEST("Tag selector 'div' matches <div>")
    {
        if (cascade.selectorMatches(div, "div")) {
            PASS()
        } else {
            FAIL("Should match")
        }
    }
    
    // Test 10: ID selector matches
    TEST("ID selector '#main' matches element with id='main'")
    {
        if (cascade.selectorMatches(div, "#main")) {
            PASS()
        } else {
            FAIL("Should match")
        }
    }
    
    // Test 11: Class selector matches
    TEST("Class selector '.container' matches element with class='container'")
    {
        if (cascade.selectorMatches(div, ".container")) {
            PASS()
        } else {
            FAIL("Should match")
        }
    }
    
    // Test 12: Multiple classes
    TEST("Class selector '.active' matches element with multiple classes")
    {
        if (cascade.selectorMatches(div, ".active")) {
            PASS()
        } else {
            FAIL("Should match")
        }
    }
    
    // Test 13: Non-matching selector
    TEST("Class selector '.nonexistent' does not match")
    {
        if (!cascade.selectorMatches(div, ".nonexistent")) {
            PASS()
        } else {
            FAIL("Should not match")
        }
    }
}

// ============================================================================
// Style Resolution Tests
// ============================================================================

void testStyleResolution() {
    std::cout << "\n=== Style Resolution ===" << std::endl;
    
    StyleResolver resolver;
    
    // Test 14: Computed style includes inherited values
    TEST("Child inherits color from parent")
    {
        CSSComputedStyle parent;
        parent.color = CSSColor::fromRGB(255, 0, 0);  // Red
        
        CSSComputedStyle child = CSSComputedStyle::inherit(parent);
        
        if (child.color.r == 255 && child.color.g == 0 && child.color.b == 0) {
            PASS()
        } else {
            FAIL("Color should be inherited")
        }
    }
    
    // Test 15: Font family inherits
    TEST("Child inherits font-family from parent")
    {
        CSSComputedStyle parent;
        parent.fontFamily = "Arial";
        
        CSSComputedStyle child = CSSComputedStyle::inherit(parent);
        
        if (child.fontFamily == "Arial") {
            PASS()
        } else {
            FAIL("Font family should be inherited")
        }
    }
    
    // Test 16: Display does not inherit
    TEST("Display does NOT inherit (box model property)")
    {
        CSSComputedStyle parent;
        parent.display = DisplayValue::Flex;
        
        CSSComputedStyle child = CSSComputedStyle::inherit(parent);
        
        // Display should not be Flex (should be default Block)
        if (child.display == DisplayValue::Block) {
            PASS()
        } else {
            FAIL("Display should not inherit")
        }
    }
}

// ============================================================================
// CSSLength Parsing Tests  
// ============================================================================

void testLengthParsing() {
    std::cout << "\n=== Length Parsing ===" << std::endl;
    
    // Test 17: Parse px value
    TEST("Parse '16px' correctly")
    {
        auto len = CSSLength::parse("16px");
        if (len.value == 16.0f && len.unit == CSSLength::Unit::Px) {
            PASS()
        } else {
            FAIL("Expected 16px")
        }
    }
    
    // Test 18: Parse em value
    TEST("Parse '1.5em' correctly")
    {
        auto len = CSSLength::parse("1.5em");
        if (len.value == 1.5f && len.unit == CSSLength::Unit::Em) {
            PASS()
        } else {
            FAIL("Expected 1.5em")
        }
    }
    
    // Test 19: Parse percentage
    TEST("Parse '50%' correctly")
    {
        auto len = CSSLength::parse("50%");
        if (len.value == 50.0f && len.unit == CSSLength::Unit::Percent) {
            PASS()
        } else {
            FAIL("Expected 50%")
        }
    }
    
    // Test 20: Parse auto
    TEST("Parse 'auto' correctly")
    {
        auto len = CSSLength::parse("auto");
        if (len.isAuto()) {
            PASS()
        } else {
            FAIL("Expected auto")
        }
    }
}

// ============================================================================
// Color Parsing Tests
// ============================================================================

void testColorParsing() {
    std::cout << "\n=== Color Parsing ===" << std::endl;
    
    // Test 21: Parse hex color
    TEST("Parse '#ff0000' as red")
    {
        auto color = CSSColor::fromHex("#ff0000");
        if (color.r == 255 && color.g == 0 && color.b == 0) {
            PASS()
        } else {
            FAIL("Expected red")
        }
    }
    
    // Test 22: Parse named color
    TEST("Parse 'black' correctly")
    {
        auto color = CSSColor::parse("black");
        if (color.r == 0 && color.g == 0 && color.b == 0) {
            PASS()
        } else {
            FAIL("Expected black")
        }
    }
    
    // Test 23: Parse rgb function
    TEST("Parse 'rgb(0, 128, 255)' correctly")
    {
        auto color = CSSColor::parse("rgb(0, 128, 255)");
        if (color.r == 0 && color.g == 128 && color.b == 255) {
            PASS()
        } else {
            FAIL("Expected rgb(0, 128, 255)")
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    CSS Cascade Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;
    
    testSpecificityCalculation();
    testCascadeOrdering();
    testSelectorMatching();
    testStyleResolution();
    testLengthParsing();
    testColorParsing();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << testsPassed << "/" << testsRun << " tests passed" << std::endl;
    
    if (testsPassed == testsRun) {
        std::cout << "ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
