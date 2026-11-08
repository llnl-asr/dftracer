#include <dftracer/core/aggregator/rules.h>
namespace dftracer {
RuleAST Rules::parseRule(const std::string& rule) {
  // Very basic parser: supports rules like "field == value", "field IN
  // {a,b}", "field LIKE pattern" For demonstration, only supports EQ, IN,
  // LIKE, AND, OR, NOT with limited syntax.
  RuleAST ast;
  std::string trimmed = rule;
  trimmed.erase(0, trimmed.find_first_not_of(" \t"));
  trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

  // Handle NOT
  if (trimmed.substr(0, 4) == "NOT ") {
    RuleAST subAst = parseRule(trimmed.substr(4));
    ast.root =
        std::make_unique<RuleAST::UnaryOp>(RuleOp::NOT, std::move(subAst.root));
    return ast;
  }

  // Handle AND/OR (only one level, left/right)
  size_t andPos = trimmed.find(" AND ");
  size_t orPos = trimmed.find(" OR ");
  if (andPos != std::string::npos) {
    RuleAST left = parseRule(trimmed.substr(0, andPos));
    RuleAST right = parseRule(trimmed.substr(andPos + 5));
    ast.root = std::make_unique<RuleAST::BinaryOp>(
        RuleOp::AND, std::move(left.root), std::move(right.root));
    return ast;
  }
  if (orPos != std::string::npos) {
    RuleAST left = parseRule(trimmed.substr(0, orPos));
    RuleAST right = parseRule(trimmed.substr(orPos + 4));
    ast.root = std::make_unique<RuleAST::BinaryOp>(
        RuleOp::OR, std::move(left.root), std::move(right.root));
    return ast;
  }

  // Handle IN
  size_t inPos = trimmed.find(" IN ");
  if (inPos != std::string::npos) {
    std::string fieldStr = trimmed.substr(0, inPos);
    std::string setStr = trimmed.substr(inPos + 4);
    if (setStr.front() == '{' && setStr.back() == '}') {
      setStr = setStr.substr(1, setStr.size() - 2);
      std::set<std::string, std::less<>> vals;
      size_t start = 0, end;
      while ((end = setStr.find(',', start)) != std::string::npos) {
        vals.insert(fixString(setStr.substr(start, end - start)));
        start = end + 1;
      }
      vals.insert(fixString(setStr.substr(start)));
      Field field;
      field.path.push_back(fieldStr);
      ast.root = std::make_unique<RuleAST::InOp>(field, vals);
      return ast;
    }
  }

  // Handle LIKE
  size_t likePos = trimmed.find(" LIKE ");
  if (likePos != std::string::npos) {
    std::string fieldStr = trimmed.substr(0, likePos);
    std::string pat = trimmed.substr(likePos + 6);
    pat = fixString(pat.substr(1, pat.size() - 2));
    Field field;
    field.path.push_back(fieldStr);
    ast.root = std::make_unique<RuleAST::LikeOp>(field, pat);
    return ast;
  }

  // Handle EQ/NEQ/GTE/LTE/GT/LT
  std::vector<std::pair<std::string, RuleOp>> ops = {
      {"==", RuleOp::EQ},  {"!=", RuleOp::NEQ}, {">=", RuleOp::GTE},
      {"<=", RuleOp::LTE}, {">", RuleOp::GT},   {"<", RuleOp::LT}};
  for (const auto& [opstr, op] : ops) {
    size_t pos = trimmed.find(opstr);
    if (pos != std::string::npos) {
      std::string fieldStr = trimmed.substr(0, pos);
      std::string valStr = trimmed.substr(pos + opstr.size());
      fieldStr.erase(0, fieldStr.find_first_not_of(" \t"));
      fieldStr.erase(fieldStr.find_last_not_of(" \t") + 1);
      valStr.erase(0, valStr.find_first_not_of(" \t"));
      valStr.erase(valStr.find_last_not_of(" \t") + 1);

      Field field;
      field.path.push_back(fieldStr);

      Value value;
      // Try to parse int/double, else treat as string
      // If valStr is quoted, treat as string
      value = stringToValue(valStr);
      ast.root = std::make_unique<RuleAST::Comparison>(op, field, value);
      return ast;
    }
  }

  // If nothing matched, return empty AST
  ast.root = nullptr;
  return ast;
}

bool Rules::eval(const RuleAST::Node* node, const AggregatedKey* key) const {
  if (!node) return true;
  if (auto bin = dynamic_cast<const RuleAST::BinaryOp*>(node)) {
    bool left = eval(bin->left.get(), key);
    bool right = eval(bin->right.get(), key);
    switch (bin->op) {
      case RuleOp::AND:
        return left && right;
      case RuleOp::OR:
        return left || right;
      default:
        return false;
    }
  }
  if (auto un = dynamic_cast<const RuleAST::UnaryOp*>(node)) {
    bool operand = eval(un->operand.get(), key);
    switch (un->op) {
      case RuleOp::NOT:
        return !operand;
      default:
        return false;
    }
  }
  if (auto cmp = dynamic_cast<const RuleAST::Comparison*>(node)) {
    auto fieldVal = getFieldValue(key, cmp->field);
    if (!fieldVal.has_value()) return false;
    bool result = compareValues(fieldVal.value(), cmp->value, cmp->op);
    return result;
  }
  if (auto inop = dynamic_cast<const RuleAST::InOp*>(node)) {
    auto fieldVal = getFieldValue(key, inop->field);
    if (!fieldVal.has_value()) return false;
    if (auto text = textOf(*fieldVal)) {
      return inop->values.count(*text) > 0;
    }
    return false;
  }
  if (auto likeop = dynamic_cast<const RuleAST::LikeOp*>(node)) {
    auto fieldVal = getFieldValue(key, likeop->field);
    if (!fieldVal.has_value()) return false;
    if (auto text = textOf(*fieldVal)) {
      return likeMatch(*text, likeop->pattern);
    }
    return false;
  }
  return false;
}
}  // namespace dftracer