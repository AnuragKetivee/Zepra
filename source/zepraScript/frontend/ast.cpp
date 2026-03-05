/**
 * @file ast.cpp
 * @brief AST node utility implementations
 */

#include "frontend/ast.hpp"

namespace Zepra::Frontend {

const char* nodeTypeName(NodeType type) {
    switch (type) {
        case NodeType::Program: return "Program";
        case NodeType::Literal: return "Literal";
        case NodeType::Identifier: return "Identifier";
        case NodeType::ArrayExpression: return "ArrayExpression";
        case NodeType::ObjectExpression: return "ObjectExpression";
        case NodeType::FunctionExpression: return "FunctionExpression";
        case NodeType::ArrowFunction: return "ArrowFunction";
        case NodeType::MemberExpression: return "MemberExpression";
        case NodeType::CallExpression: return "CallExpression";
        case NodeType::NewExpression: return "NewExpression";
        case NodeType::UnaryExpression: return "UnaryExpression";
        case NodeType::BinaryExpression: return "BinaryExpression";
        case NodeType::LogicalExpression: return "LogicalExpression";
        case NodeType::ConditionalExpression: return "ConditionalExpression";
        case NodeType::AssignmentExpression: return "AssignmentExpression";
        case NodeType::SequenceExpression: return "SequenceExpression";
        case NodeType::UpdateExpression: return "UpdateExpression";
        case NodeType::ThisExpression: return "ThisExpression";
        case NodeType::SpreadElement: return "SpreadElement";
        case NodeType::TemplateLiteral: return "TemplateLiteral";
        case NodeType::TaggedTemplateExpression: return "TaggedTemplateExpression";
        case NodeType::ClassExpression: return "ClassExpression";
        case NodeType::AwaitExpression: return "AwaitExpression";
        case NodeType::YieldExpression: return "YieldExpression";
        case NodeType::BlockStatement: return "BlockStatement";
        case NodeType::ExpressionStatement: return "ExpressionStatement";
        case NodeType::EmptyStatement: return "EmptyStatement";
        case NodeType::ReturnStatement: return "ReturnStatement";
        case NodeType::BreakStatement: return "BreakStatement";
        case NodeType::ContinueStatement: return "ContinueStatement";
        case NodeType::IfStatement: return "IfStatement";
        case NodeType::SwitchStatement: return "SwitchStatement";
        case NodeType::WhileStatement: return "WhileStatement";
        case NodeType::DoWhileStatement: return "DoWhileStatement";
        case NodeType::ForStatement: return "ForStatement";
        case NodeType::ForInStatement: return "ForInStatement";
        case NodeType::ForOfStatement: return "ForOfStatement";
        case NodeType::ThrowStatement: return "ThrowStatement";
        case NodeType::TryStatement: return "TryStatement";
        case NodeType::DebuggerStatement: return "DebuggerStatement";
        case NodeType::LabeledStatement: return "LabeledStatement";
        case NodeType::WithStatement: return "WithStatement";
        case NodeType::VariableDeclaration: return "VariableDeclaration";
        case NodeType::FunctionDeclaration: return "FunctionDeclaration";
        case NodeType::ClassDeclaration: return "ClassDeclaration";
        case NodeType::ImportDeclaration: return "ImportDeclaration";
        case NodeType::ExportDeclaration: return "ExportDeclaration";
        case NodeType::ObjectPattern: return "ObjectPattern";
        case NodeType::ArrayPattern: return "ArrayPattern";
        case NodeType::RestElement: return "RestElement";
        default: return "Unknown";
    }
}

} // namespace Zepra::Frontend
