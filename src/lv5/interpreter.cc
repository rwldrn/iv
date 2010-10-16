#include <cstdio>
#include <cassert>
#include <cmath>
#include <iostream>  // NOLINT
#include <tr1/tuple>
#include <tr1/array>
#include <boost/foreach.hpp>
#include "token.h"
#include "size_t.h"
#include "interpreter.h"
#include "scope.h"
#include "jsreference.h"
#include "jsobject.h"
#include "jsstring.h"
#include "jsfunction.h"
#include "jsregexp.h"
#include "jsexception.h"
#include "jsproperty.h"
#include "jsenv.h"
#include "jsarray.h"
#include "ustream.h"
#include "context.h"

namespace iv {
namespace lv5 {

#define CHECK  ctx_->error());\
  if (ctx_->IsError()) {\
    return;\
  }\
  ((void)0


#define CHECK_WITH(val) ctx_->error());\
  if (ctx_->IsError()) {\
    return val;\
  }\
  ((void)0


#define CHECK_TO_WITH(error, val) error);\
  if (*error) {\
    return val;\
  }\
  ((void)0


#define RETURN_STMT(type, val, target)\
  do {\
    ctx_->SetStatement(type, val, target);\
    return;\
  } while (0)


#define ABRUPT()\
  do {\
    return;\
  } while (0)


#define EVAL(node)\
  node->Accept(this);\
  if (ctx_->IsError()) {\
    return;\
  }

Interpreter::ContextSwitcher::ContextSwitcher(Context* ctx,
                                              JSEnv* lex,
                                              JSEnv* var,
                                              JSObject* binding,
                                              bool strict)
  : prev_lex_(ctx->lexical_env()),
    prev_var_(ctx->variable_env()),
    prev_binding_(ctx->this_binding()),
    prev_strict_(strict),
    ctx_(ctx) {
  ctx_->set_lexical_env(lex);
  ctx_->set_variable_env(var);
  ctx_->set_this_binding(binding);
  ctx_->set_strict(strict);
}

Interpreter::ContextSwitcher::~ContextSwitcher() {
  ctx_->set_lexical_env(prev_lex_);
  ctx_->set_variable_env(prev_var_);
  ctx_->set_this_binding(prev_binding_);
  ctx_->set_strict(prev_strict_);
}

Interpreter::LexicalEnvSwitcher::LexicalEnvSwitcher(Context* context,
                                                    JSEnv* env)
  : ctx_(context),
    old_(context->lexical_env()) {
  ctx_->set_lexical_env(env);
}

Interpreter::LexicalEnvSwitcher::~LexicalEnvSwitcher() {
  ctx_->set_lexical_env(old_);
}

Interpreter::StrictSwitcher::StrictSwitcher(Context* ctx, bool strict)
  : ctx_(ctx),
    prev_(ctx->IsStrict()) {
  ctx_->set_strict(strict);
}

Interpreter::StrictSwitcher::~StrictSwitcher() {
  ctx_->set_strict(prev_);
}

Interpreter::Interpreter()
  : ctx_(NULL) {
}


Interpreter::~Interpreter() {
}


// section 13.2.1 [[Call]]
void Interpreter::CallCode(
    const JSCodeFunction& code,
    const Arguments& args,
    JSErrorCode::Type* error) {
  // step 1
  JSVal this_value = args.this_binding();
  if (this_value.IsUndefined()) {
    this_value.set_value(ctx_->global_obj());
  } else if (!this_value.IsObject()) {
    JSObject* obj = this_value.ToObject(ctx_, CHECK);
    this_value.set_value(obj);
  }
  JSDeclEnv* local_env = NewDeclarativeEnvironment(ctx_, code.scope());
  ContextSwitcher switcher(ctx_, local_env,
                           local_env, this_value.object(),
                           code.code()->strict());

  // section 10.5 Declaration Binding Instantiation
  const core::Scope* const scope = code.code()->scope();
  // step 1
  JSEnv* const env = ctx_->variable_env();
  // step 2
  const bool configurable_bindings = false;
  // step 4
  {
    Arguments::JSVals arguments = args.args();
    std::size_t arg_count = arguments.size();
    std::size_t n = 0;
    BOOST_FOREACH(core::Identifier* const ident,
                  code.code()->params()) {
      ++n;
      Symbol arg_name = ctx_->Intern(ident->value());
      if (!env->HasBinding(arg_name)) {
        env->CreateMutableBinding(ctx_, arg_name, configurable_bindings);
      }
      if (n > arg_count) {
        env->SetMutableBinding(ctx_, arg_name,
                               JSVal::Undefined(), ctx_->IsStrict(), CHECK);
      } else {
        env->SetMutableBinding(ctx_, arg_name,
                               arguments[n], ctx_->IsStrict(), CHECK);
      }
    }
  }
  BOOST_FOREACH(core::FunctionLiteral* const f,
                scope->function_declarations()) {
    const Symbol fn = ctx_->Intern(f->name()->value());
    EVAL(f);
    JSVal fo = ctx_->ret();
    if (!env->HasBinding(fn)) {
      env->CreateMutableBinding(ctx_, fn, configurable_bindings);
    }
    env->SetMutableBinding(ctx_, fn, fo, ctx_->IsStrict(), CHECK);
  }
  BOOST_FOREACH(const core::Scope::Variable& var, scope->variables()) {
    const Symbol dn = ctx_->Intern(var.first->value());
    if (!env->HasBinding(dn)) {
      env->CreateMutableBinding(ctx_, dn, configurable_bindings);
      env->SetMutableBinding(ctx_, dn,
                             JSVal::Undefined(), ctx_->IsStrict(), CHECK);
    }
  }

  JSVal value;
  BOOST_FOREACH(core::Statement* const stmt, code.code()->body()) {
    EVAL(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      // section 12.1 step 4
      // TODO(Constellation) value to exception
      RETURN_STMT(Context::THROW, value, NULL);
      return;
    }
    if (!ctx_->ret().IsUndefined()) {
      value = ctx_->ret();
    }
    if (!ctx_->IsMode<Context::NORMAL>()) {
      ABRUPT();
    }
  }
}


void Interpreter::Run(core::FunctionLiteral* global) {
  // section 10.5 Declaration Binding Instantiation
  const bool configurable_bindings = false;
  const core::Scope* const scope = global->scope();
  JSEnv* const env = ctx_->variable_env();
  StrictSwitcher switcher(ctx_, global->strict());
  BOOST_FOREACH(core::FunctionLiteral* const f,
                scope->function_declarations()) {
    const Symbol fn = ctx_->Intern(f->name()->value());
    EVAL(f);
    JSVal fo = ctx_->ret();
    if (!env->HasBinding(fn)) {
      env->CreateMutableBinding(ctx_, fn, configurable_bindings);
    }
    env->SetMutableBinding(ctx_, fn, fo, ctx_->IsStrict(), CHECK);
  }

  BOOST_FOREACH(const core::Scope::Variable& var, scope->variables()) {
    const Symbol dn = ctx_->Intern(var.first->value());
    if (!env->HasBinding(dn)) {
      env->CreateMutableBinding(ctx_, dn, configurable_bindings);
      env->SetMutableBinding(ctx_, dn,
                             JSVal::Undefined(), ctx_->IsStrict(), CHECK);
    }
  }

  JSVal value;
  // section 14 Program
  BOOST_FOREACH(core::Statement* const stmt, global->body()) {
    EVAL(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      // section 12.1 step 4
      // TODO(Constellation) value to exception
      RETURN_STMT(Context::THROW, value, NULL);
      return;
    }
    if (!ctx_->ret().IsUndefined()) {
      value = ctx_->ret();
    }
    if (!ctx_->IsMode<Context::NORMAL>()) {
      ABRUPT();
    }
  }
  return;
}


void Interpreter::Visit(core::Block* block) {
  // section 12.1 Block
  ctx_->set_mode(Context::Context::NORMAL);
  JSVal value;
  BOOST_FOREACH(core::Statement* const stmt, block->body()) {
    EVAL(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      // section 12.1 step 4
      // TODO(Constellation) value to exception
      RETURN_STMT(Context::THROW, value, NULL);
      return;
    }
    if (!ctx_->ret().IsUndefined()) {
      value = ctx_->ret();
    }

    if (ctx_->IsMode<Context::BREAK>() &&
        ctx_->InCurrentLabelSet(block)) {
      RETURN_STMT(Context::NORMAL, value, NULL);
    }
    if (!ctx_->IsMode<Context::NORMAL>()) {
      ABRUPT();
    }
  }
  ctx_->Return(value);
}


void Interpreter::Visit(core::FunctionStatement* stmt) {
  core::FunctionLiteral* const func = stmt->function();
  func->name()->Accept(this);
  const JSVal lhs = ctx_->ret();
  EVAL(func);
  const JSVal val = GetValue(ctx_->ret(), CHECK);
  PutValue(lhs, val, CHECK);
}


void Interpreter::Visit(core::VariableStatement* var) {
  // bool is_const = var->IsConst();
  BOOST_FOREACH(const core::Declaration* const decl, var->decls()) {
    EVAL(decl->name());
    const JSVal lhs = ctx_->ret();
    if (decl->expr()) {
      EVAL(decl->expr());
      const JSVal val = GetValue(ctx_->ret(), CHECK);
      PutValue(lhs, val, CHECK);
      // TODO(Constellation) 12.2 step 5 Return a String value
    }
  }
}


void Interpreter::Visit(core::Declaration* decl) {
  UNREACHABLE();
}


void Interpreter::Visit(core::EmptyStatement* empty) {
  RETURN_STMT(Context::NORMAL, JSVal::Undefined(), NULL);
}


void Interpreter::Visit(core::IfStatement* stmt) {
  EVAL(stmt->cond());
  const JSVal expr = GetValue(ctx_->ret(), CHECK);
  const bool val = expr.ToBoolean(CHECK);
  if (val) {
    EVAL(stmt->then_statement());
    return;
  } else {
    core::Statement* const else_stmt = stmt->else_statement();
    if (else_stmt) {
      EVAL(else_stmt);
      return;
    } else {
      RETURN_STMT(Context::NORMAL, JSVal::Undefined(), NULL);
    }
  }
}


void Interpreter::Visit(core::DoWhileStatement* stmt) {
  JSVal value;
  bool iterating = true;
  while (iterating) {
    EVAL(stmt->body());
    if (!ctx_->ret().IsUndefined()) {
      value = ctx_->ret();
    }
    if (!ctx_->IsMode<Context::CONTINUE>() ||
        !ctx_->InCurrentLabelSet(stmt)) {
      if (ctx_->IsMode<Context::BREAK>() &&
          ctx_->InCurrentLabelSet(stmt)) {
        RETURN_STMT(Context::NORMAL, value, NULL);
      }
      if (!ctx_->IsMode<Context::NORMAL>()) {
        ABRUPT();
      }
    }
    EVAL(stmt->cond());
    const JSVal expr = GetValue(ctx_->ret(), CHECK);
    const bool val = expr.ToBoolean(CHECK);
    iterating = val;
  }
  RETURN_STMT(Context::NORMAL, value, NULL);
}


void Interpreter::Visit(core::WhileStatement* stmt) {
  JSVal value;
  while (true) {
    EVAL(stmt->cond());
    const JSVal expr = GetValue(ctx_->ret(), CHECK);
    const bool val = expr.ToBoolean(CHECK);
    if (val) {
      EVAL(stmt->body());
      if (!ctx_->ret().IsUndefined()) {
        value = ctx_->ret();
      }
      if (!ctx_->IsMode<Context::CONTINUE>() ||
          !ctx_->InCurrentLabelSet(stmt)) {
        if (ctx_->IsMode<Context::BREAK>() &&
            ctx_->InCurrentLabelSet(stmt)) {
          RETURN_STMT(Context::NORMAL, value, NULL);
        }
        if (!ctx_->IsMode<Context::NORMAL>()) {
          ABRUPT();
        }
      }
    } else {
      RETURN_STMT(Context::NORMAL, value, NULL);
    }
  }
}


void Interpreter::Visit(core::ForStatement* stmt) {
  if (stmt->init()) {
    EVAL(stmt->init());
    GetValue(ctx_->ret(), CHECK);
  }
  JSVal value;
  while (true) {
    if (stmt->cond()) {
      EVAL(stmt->cond());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const bool val = expr.ToBoolean(CHECK);
      if (!val) {
        RETURN_STMT(Context::NORMAL, value, NULL);
      }
    }
    EVAL(stmt->body());
    if (!ctx_->ret().IsUndefined()) {
      value = ctx_->ret();
    }
    if (!ctx_->IsMode<Context::CONTINUE>() ||
        !ctx_->InCurrentLabelSet(stmt)) {
      if (ctx_->IsMode<Context::BREAK>() &&
          ctx_->InCurrentLabelSet(stmt)) {
        RETURN_STMT(Context::NORMAL, value, NULL);
      }
      if (!ctx_->IsMode<Context::NORMAL>()) {
        ABRUPT();
      }
    }
    if (stmt->next()) {
      EVAL(stmt->next());
      GetValue(ctx_->ret(), CHECK);
    }
  }
}


void Interpreter::Visit(core::ForInStatement* stmt) {
  EVAL(stmt->enumerable());
  JSVal expr = GetValue(ctx_->ret(), CHECK);
  if (expr.IsNull() || expr.IsUndefined()) {
    RETURN_STMT(Context::NORMAL, JSVal::Undefined(), NULL);
  }
  JSObject* const obj = expr.ToObject(ctx_, CHECK);
  JSVal value;
  JSObject* current = obj;
  do {
    BOOST_FOREACH(const JSObject::Properties::value_type& set,
                  current->table()) {
      if (!set.second->IsEnumerable()) {
        continue;
      }
      JSVal rhs(ctx_->ToString(set.first));
      EVAL(stmt->each());
      if (stmt->each()->AsVariableStatement()) {
        core::Identifier* ident =
            stmt->each()->AsVariableStatement()->decls().front()->name();
        EVAL(ident);
      }
      JSVal lhs = ctx_->ret();
      PutValue(lhs, rhs, CHECK);
      EVAL(stmt->body());
      if (!ctx_->ret().IsUndefined()) {
        value = ctx_->ret();
      }
      if (!ctx_->IsMode<Context::CONTINUE>() ||
          !ctx_->InCurrentLabelSet(stmt)) {
        if (ctx_->IsMode<Context::BREAK>() &&
            ctx_->InCurrentLabelSet(stmt)) {
          RETURN_STMT(Context::NORMAL, value, NULL);
        }
        if (!ctx_->IsMode<Context::NORMAL>()) {
          ABRUPT();
        }
      }
    }
    current = current->prototype();
  } while (current);
  RETURN_STMT(Context::NORMAL, value, NULL);
}


void Interpreter::Visit(core::ContinueStatement* stmt) {
  RETURN_STMT(Context::CONTINUE, JSVal::Undefined(), stmt->target());
}


void Interpreter::Visit(core::BreakStatement* stmt) {
  if (stmt->target()) {
  } else {
    if (stmt->label()) {
      RETURN_STMT(Context::NORMAL, JSVal::Undefined(), NULL);
    }
  }
  RETURN_STMT(Context::BREAK, JSVal::Undefined(), stmt->target());
}


void Interpreter::Visit(core::ReturnStatement* stmt) {
  if (stmt->expr()) {
    EVAL(stmt->expr());
    const JSVal value = GetValue(ctx_->ret(), CHECK);
    RETURN_STMT(Context::RETURN, value, NULL);
  } else {
    RETURN_STMT(Context::RETURN, JSVal::Undefined(), NULL);
  }
}


// section 12.10 The with Statement
void Interpreter::Visit(core::WithStatement* stmt) {
  EVAL(stmt->context());
  const JSVal val = GetValue(ctx_->ret(), CHECK);
  JSObject* const obj = val.ToObject(ctx_, CHECK);
  JSEnv* const old_env = ctx_->lexical_env();
  JSObjectEnv* const new_env = NewObjectEnvironment(ctx_, obj, old_env);
  new_env->set_provide_this(true);
  {
    LexicalEnvSwitcher switcher(ctx_, new_env);
    EVAL(stmt->body());
  }
}


void Interpreter::Visit(core::LabelledStatement* stmt) {
  EVAL(stmt->body());
}


void Interpreter::Visit(core::CaseClause* clause) {
  UNREACHABLE();
}


void Interpreter::Visit(core::SwitchStatement* stmt) {
  EVAL(stmt->expr());
  const JSVal cond = GetValue(ctx_->ret(), CHECK);
  // Case Block
  JSVal value;
  {
    typedef core::SwitchStatement::CaseClauses CaseClauses;
    using core::CaseClause;
    bool found = false;
    bool default_found = false;
    bool finalize = false;
    const CaseClauses& clauses = stmt->clauses();
    CaseClauses::const_iterator default_it = clauses.end();
    for (CaseClauses::const_iterator it = clauses.begin(),
         last = clauses.end(); it != last; ++it) {
      const CaseClause* const clause = *it;
      if (clause->IsDefault()) {
        default_it = it;
        default_found = true;
      } else {
        if (!found) {
          EVAL(clause->expr());
          const JSVal res = GetValue(ctx_->ret(), CHECK);
          if (StrictEqual(cond, res)) {
            found = true;
          }
        }
      }
      // case's fall through
      if (found) {
        BOOST_FOREACH(core::Statement* const st, clause->body()) {
          EVAL(st);
          if (!ctx_->ret().IsUndefined()) {
            value = ctx_->ret();
          }
          if (!ctx_->IsMode<Context::NORMAL>()) {
            ctx_->ret() = value;
            finalize = true;
            break;
          }
        }
        if (finalize) {
          break;
        }
      }
    }
    if (!finalize && !found && default_found) {
      for (CaseClauses::const_iterator it = default_it,
           last = clauses.end(); it != last; ++it) {
        BOOST_FOREACH(core::Statement* const st, (*it)->body()) {
          EVAL(st);
          if (!ctx_->ret().IsUndefined()) {
            value = ctx_->ret();
          }
          if (!ctx_->IsMode<Context::NORMAL>()) {
            ctx_->ret() = value;
            finalize = true;
            break;
          }
        }
        if (finalize) {
          break;
        }
      }
    }
  }

  if (ctx_->IsMode<Context::BREAK>() && ctx_->InCurrentLabelSet(stmt)) {
    RETURN_STMT(Context::NORMAL, value, NULL);
  }
}


// section 12.13 The throw Statement
void Interpreter::Visit(core::ThrowStatement* stmt) {
  EVAL(stmt->expr());
  JSVal ref = GetValue(ctx_->ret(), CHECK);
  RETURN_STMT(Context::THROW, ref, NULL);
}


// section 12.14 The try Statement
void Interpreter::Visit(core::TryStatement* stmt) {
  stmt->body()->Accept(this);
  if (ctx_->IsMode<Context::THROW>() || ctx_->IsError()) {
    if (stmt->catch_block()) {
      ctx_->set_mode(Context::NORMAL);
      ctx_->set_error(JSErrorCode::Normal);
      JSEnv* const old_env = ctx_->lexical_env();
      JSEnv* const catch_env = NewDeclarativeEnvironment(ctx_, old_env);
      const Symbol name = ctx_->Intern(stmt->catch_name()->value());
      const JSVal ex = (ctx_->IsMode<Context::THROW>()) ?
          ctx_->ret() : JSVal::Undefined();
      catch_env->CreateMutableBinding(ctx_, name, false);
      catch_env->SetMutableBinding(ctx_, name, ex, false, CHECK);
      {
        LexicalEnvSwitcher switcher(ctx_, catch_env);
        EVAL(stmt->catch_block());
      }
    }
  }
  const Context::Mode mode = ctx_->mode();
  const JSVal value = ctx_->ret();
  core::BreakableStatement* const target = ctx_->target();

  ctx_->set_error(JSErrorCode::Normal);
  ctx_->SetStatement(Context::Context::NORMAL, JSVal::Undefined(), NULL);

  if (stmt->finally_block()) {
    stmt->finally_block()->Accept(this);
    if (ctx_->IsMode<Context::NORMAL>()) {
      RETURN_STMT(mode, value, target);
    }
  }
}


void Interpreter::Visit(core::DebuggerStatement* stmt) {
  // section 12.15 debugger statement
  // implementation define debugging facility is not available
  RETURN_STMT(Context::NORMAL, JSVal::Undefined(), NULL);
}


void Interpreter::Visit(core::ExpressionStatement* stmt) {
  EVAL(stmt->expr());
  const JSVal value = GetValue(ctx_->ret(), CHECK);
  RETURN_STMT(Context::NORMAL, value, NULL);
}


void Interpreter::Visit(core::Assignment* assign) {
  using core::Token;
  EVAL(assign->left());
  const JSVal lref(ctx_->ret());
  const JSVal lhs = GetValue(lref, CHECK);
  EVAL(assign->right());
  const JSVal rhs = GetValue(ctx_->ret(), CHECK);
  JSVal result;
  switch (assign->op()) {
    case Token::ASSIGN: {  // =
      result = rhs;
      break;
    }
    case Token::ASSIGN_ADD: {  // +=
      const JSVal lprim = lhs.ToPrimitive(ctx_, JSObject::NONE, CHECK);
      const JSVal rprim = rhs.ToPrimitive(ctx_, JSObject::NONE, CHECK);
      if (lprim.IsString() || rprim.IsString()) {
        const JSString* const lstr = lprim.ToString(ctx_, CHECK);
        const JSString* const rstr = rprim.ToString(ctx_, CHECK);
        ctx_->Return(JSString::New(ctx_, *lstr + *rstr));
        return;
      }
      assert(lprim.IsNumber() && rprim.IsNumber());
      const double left_num = lprim.ToNumber(ctx_, CHECK);
      const double right_num = rprim.ToNumber(ctx_, CHECK);
      result.set_value(left_num + right_num);
      break;
    }
    case Token::ASSIGN_SUB: {  // -=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(left_num - right_num);
      break;
    }
    case Token::ASSIGN_MUL: {  // *=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(left_num * right_num);
      break;
    }
    case Token::ASSIGN_MOD: {  // %=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(std::fmod(left_num, right_num));
      break;
    }
    case Token::ASSIGN_DIV: {  // /=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(left_num / right_num);
      break;
    }
    case Token::ASSIGN_SAR: {  // >>=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(
          static_cast<double>(core::Conv::DoubleToInt32(left_num)
                 >> (core::Conv::DoubleToInt32(right_num) & 0x1f)));
      break;
    }
    case Token::ASSIGN_SHR: {  // >>>=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(
          static_cast<double>(core::Conv::DoubleToUInt32(left_num)
                 >> (core::Conv::DoubleToInt32(right_num) & 0x1f)));
      break;
    }
    case Token::ASSIGN_SHL: {  // <<=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(
          static_cast<double>(core::Conv::DoubleToInt32(left_num)
                 << (core::Conv::DoubleToInt32(right_num) & 0x1f)));
      break;
    }
    case Token::ASSIGN_BIT_AND: {  // &=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(
          static_cast<double>(core::Conv::DoubleToInt32(left_num)
                 & (core::Conv::DoubleToInt32(right_num))));
      break;
    }
    case Token::ASSIGN_BIT_OR: {  // |=
      const double left_num = lhs.ToNumber(ctx_, CHECK);
      const double right_num = rhs.ToNumber(ctx_, CHECK);
      result.set_value(
          static_cast<double>(core::Conv::DoubleToInt32(left_num)
                 | (core::Conv::DoubleToInt32(right_num))));
      break;
    }
    default: {
      UNREACHABLE();
      break;
    }
  }
  if (lref.IsReference()) {
    const JSReference* const ref = lref.reference();
    if (ref->IsStrictReference() &&
        ref->base()->IsEnvironment()) {
      const Symbol sym = ref->GetReferencedName();
      if (sym == ctx_->eval_symbol() ||
          sym == ctx_->arguments_symbol()) {
        ctx_->set_error(JSErrorCode::SyntaxError);
        return;
      }
    }
  }
  PutValue(lref, result, CHECK);
  ctx_->Return(result);
}


void Interpreter::Visit(core::BinaryOperation* binary) {
  using core::Token;
  const Token::Type token = binary->op();
  EVAL(binary->left());
  const JSVal lhs = GetValue(ctx_->ret(), CHECK);
  {
    switch (token) {
      case Token::LOGICAL_AND: {  // &&
        const bool cond = lhs.ToBoolean(CHECK);
        if (!cond) {
          ctx_->Return(lhs);
          return;
        } else {
          EVAL(binary->right());
          ctx_->ret() = GetValue(ctx_->ret(), CHECK);
          return;
        }
      }

      case Token::LOGICAL_OR: {  // ||
        const bool cond = lhs.ToBoolean(CHECK);
        if (cond) {
          ctx_->Return(lhs);
          return;
        } else {
          EVAL(binary->right());
          ctx_->ret() = GetValue(ctx_->ret(), CHECK);
          return;
        }
      }

      default:
        break;
        // pass
    }
  }

  {
    EVAL(binary->right());
    const JSVal rhs = GetValue(ctx_->ret(), CHECK);
    switch (token) {
      case Token::MUL: {  // *
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(left_num * right_num);
        return;
      }

      case Token::DIV: {  // /
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(left_num / right_num);
        return;
      }

      case Token::MOD: {  // %
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(std::fmod(left_num, right_num));
        return;
      }

      case Token::ADD: {  // +
        // section 11.6.1 NOTE
        // no hint is provided in the calls to ToPrimitive
        const JSVal lprim = lhs.ToPrimitive(ctx_, JSObject::NONE, CHECK);
        const JSVal rprim = rhs.ToPrimitive(ctx_, JSObject::NONE, CHECK);
        if (lprim.IsString() || rprim.IsString()) {
          const JSString* const lstr = lprim.ToString(ctx_, CHECK);
          const JSString* const rstr = rprim.ToString(ctx_, CHECK);
          ctx_->Return(JSString::New(ctx_, *lstr + *rstr));
          return;
        }
        assert(lprim.IsNumber() && rprim.IsNumber());
        const double left_num = lprim.ToNumber(ctx_, CHECK);
        const double right_num = rprim.ToNumber(ctx_, CHECK);
        ctx_->Return(left_num + right_num);
        return;
      }

      case Token::SUB: {  // -
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(left_num - right_num);
        return;
      }

      case Token::SHL: {  // <<
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToInt32(left_num)
                   << (core::Conv::DoubleToInt32(right_num) & 0x1f)));
        return;
      }

      case Token::SAR: {  // >>
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToInt32(left_num)
                   >> (core::Conv::DoubleToInt32(right_num) & 0x1f)));
        return;
      }

      case Token::SHR: {  // >>>
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToUInt32(left_num)
                   >> (core::Conv::DoubleToInt32(right_num) & 0x1f)));
        return;
      }

      case Token::LT: {  // <
        const CompareKind res = Compare(lhs, rhs, true, CHECK);
        ctx_->Return(res == CMP_TRUE);
        return;
      }

      case Token::GT: {  // >
        const CompareKind res = Compare(rhs, lhs, false, CHECK);
        ctx_->Return(res == CMP_TRUE);
        return;
      }

      case Token::LTE: {  // <=
        const CompareKind res = Compare(rhs, lhs, false, CHECK);
        ctx_->Return(res == CMP_FALSE);
        return;
      }

      case Token::GTE: {  // >=
        const CompareKind res = Compare(lhs, rhs, true, CHECK);
        ctx_->Return(res == CMP_FALSE);
        return;
      }

      case Token::INSTANCEOF: {  // instanceof
        if (!rhs.IsObject()) {
          ctx_->set_error(JSErrorCode::TypeError);
          return;
        }
        JSObject* const robj = rhs.object();
        if (!robj->IsCallable()) {
          ctx_->set_error(JSErrorCode::TypeError);
          return;
        }
        bool res = robj->AsCallable()->HasInstance(ctx_, lhs, CHECK);
        ctx_->Return(res);
        return;
      }

      case Token::IN: {  // in
        if (!rhs.IsObject()) {
          ctx_->set_error(JSErrorCode::TypeError);
          return;
        }
        const JSString* const name = lhs.ToString(ctx_, CHECK);
        ctx_->Return(
            rhs.object()->HasProperty(ctx_->Intern(*name)));
        return;
      }

      case Token::EQ: {  // ==
        const bool res = AbstractEqual(lhs, rhs, CHECK);
        ctx_->Return(res);
        return;
      }

      case Token::NE: {  // !=
        const bool res = AbstractEqual(lhs, rhs, CHECK);
        ctx_->Return(!res);
        return;
      }

      case Token::EQ_STRICT: {  // ===
        ctx_->Return(StrictEqual(lhs, rhs));
        return;
      }

      case Token::NE_STRICT: {  // !==
        ctx_->Return(!StrictEqual(lhs, rhs));
        return;
      }

      // bitwise op
      case Token::BIT_AND: {  // &
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToInt32(left_num)
                   & (core::Conv::DoubleToInt32(right_num))));
        return;
      }

      case Token::BIT_XOR: {  // ^
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToInt32(left_num)
                   ^ (core::Conv::DoubleToInt32(right_num))));
        return;
      }

      case Token::BIT_OR: {  // |
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(
            static_cast<double>(core::Conv::DoubleToInt32(left_num)
                   | (core::Conv::DoubleToInt32(right_num))));
        return;
      }

      case Token::COMMA:  // ,
        ctx_->Return(rhs);
        return;

      default:
        return;
    }
  }
}


void Interpreter::Visit(core::ConditionalExpression* cond) {
  EVAL(cond->cond());
  const JSVal expr = GetValue(ctx_->ret(), CHECK);
  const bool condition = expr.ToBoolean(CHECK);
  if (condition) {
    EVAL(cond->left());
    ctx_->ret() = GetValue(ctx_->ret(), CHECK);
    return;
  } else {
    EVAL(cond->right());
    ctx_->ret() = GetValue(ctx_->ret(), CHECK);
    return;
  }
}


void Interpreter::Visit(core::UnaryOperation* unary) {
  using core::Token;
  switch (unary->op()) {
    case Token::DELETE: {
      EVAL(unary->expr());
      if (!ctx_->ret().IsReference()) {
        ctx_->Return(true);
        return;
      }
      const JSReference* const ref = ctx_->ret().reference();
      if (ref->IsUnresolvableReference()) {
        if (ref->IsStrictReference()) {
          ctx_->set_error(JSErrorCode::SyntaxError);
          return;
        } else {
          ctx_->Return(true);
          return;
        }
      }
      if (ref->IsPropertyReference()) {
        JSObject* const obj = ref->base()->ToObject(ctx_, CHECK);
        const bool result = obj->Delete(ref->GetReferencedName(),
                                        ref->IsStrictReference(), CHECK);
        ctx_->Return(result);
      } else {
        assert(ref->base()->IsEnvironment());
        if (ref->IsStrictReference()) {
          ctx_->set_error(JSErrorCode::SyntaxError);
          return;
        }
        ctx_->Return(
            ref->base()->environment()->DeleteBinding(
                ref->GetReferencedName()));
      }
      return;
    }

    case Token::VOID: {
      EVAL(unary->expr());
      GetValue(ctx_->ret(), CHECK);
      ctx_->ret().set_undefined();
      return;
    }

    case Token::TYPEOF: {
      EVAL(unary->expr());
      if (ctx_->ret().IsReference()) {
        if (ctx_->ret().reference()->base()->IsUndefined()) {
          ctx_->Return(
              JSString::NewAsciiString(ctx_, "undefined"));
          return;
        }
      }
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      ctx_->Return(expr.TypeOf(ctx_));
      return;
    }

    case Token::INC: {
      EVAL(unary->expr());
      const JSVal expr = ctx_->ret();
      if (expr.IsReference()) {
        JSReference* const ref = expr.reference();
        if (ref->IsStrictReference() &&
            ref->base()->IsEnvironment()) {
          const Symbol sym = ref->GetReferencedName();
          if (sym == ctx_->eval_symbol() ||
              sym == ctx_->arguments_symbol()) {
            ctx_->set_error(JSErrorCode::SyntaxError);
            return;
          }
        }
      }
      const JSVal value = GetValue(expr, CHECK);
      const double old_value = value.ToNumber(ctx_, CHECK);
      const JSVal new_value(old_value + 1);
      PutValue(expr, new_value, CHECK);
      ctx_->Return(new_value);
      return;
    }

    case Token::DEC: {
      EVAL(unary->expr());
      const JSVal expr = ctx_->ret();
      if (expr.IsReference()) {
        JSReference* const ref = expr.reference();
        if (ref->IsStrictReference() &&
            ref->base()->IsEnvironment()) {
          const Symbol sym = ref->GetReferencedName();
          if (sym == ctx_->eval_symbol() ||
              sym == ctx_->arguments_symbol()) {
            ctx_->set_error(JSErrorCode::SyntaxError);
            return;
          }
        }
      }
      const JSVal value = GetValue(expr, CHECK);
      const double old_value = value.ToNumber(ctx_, CHECK);
      const JSVal new_value(old_value - 1);
      PutValue(expr, new_value, CHECK);
      ctx_->Return(new_value);
      return;
    }

    case Token::ADD: {
      EVAL(unary->expr());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const double val = expr.ToNumber(ctx_, CHECK);
      ctx_->Return(val);
      return;
    }

    case Token::SUB: {
      EVAL(unary->expr());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const double old_value = expr.ToNumber(ctx_, CHECK);
      ctx_->Return(-old_value);
      return;
    }

    case Token::BIT_NOT: {
      EVAL(unary->expr());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const double value = expr.ToNumber(ctx_, CHECK);
      ctx_->Return(
          static_cast<double>(~core::Conv::DoubleToInt32(value)));
      return;
    }

    case Token::NOT: {
      EVAL(unary->expr());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const bool value = expr.ToBoolean(CHECK);
      ctx_->Return(!value);
      return;
    }

    default:
      UNREACHABLE();
  }
}


void Interpreter::Visit(core::PostfixExpression* postfix) {
  EVAL(postfix->expr());
  JSVal lref = ctx_->ret();
  if (lref.IsReference()) {
    const JSReference* const ref = lref.reference();
    if (ref->IsStrictReference() &&
        ref->base()->IsEnvironment()) {
      const Symbol sym = ref->GetReferencedName();
      if (sym == ctx_->eval_symbol() ||
          sym == ctx_->arguments_symbol()) {
        ctx_->set_error(JSErrorCode::SyntaxError);
        return;
      }
    }
  }
  JSVal old = GetValue(lref, CHECK);
  const double& value = old.ToNumber(ctx_, CHECK);
  const double new_value = value +
      ((postfix->op() == core::Token::INC) ? 1 : -1);
  PutValue(lref, JSVal(new_value), CHECK);
  ctx_->Return(old);
}


void Interpreter::Visit(core::StringLiteral* str) {
  ctx_->Return(JSString::New(ctx_, str->value()));
}


void Interpreter::Visit(core::NumberLiteral* num) {
  ctx_->Return(num->value());
}


void Interpreter::Visit(core::Identifier* ident) {
  // section 10.3.1 Identifier Resolution
  JSEnv* env = ctx_->lexical_env();
  ctx_->Return(
      GetIdentifierReference(env,
                             ctx_->Intern(ident->value()),
                             ctx_->IsStrict()));
}


void Interpreter::Visit(core::ThisLiteral* literal) {
  ctx_->Return(ctx_->this_binding());
}


void Interpreter::Visit(core::NullLiteral* lit) {
  ctx_->ret().set_null();
}


void Interpreter::Visit(core::TrueLiteral* lit) {
  ctx_->Return(true);
}


void Interpreter::Visit(core::FalseLiteral* lit) {
  ctx_->Return(false);
}


void Interpreter::Visit(core::Undefined* lit) {
  ctx_->ret().set_undefined();
}


void Interpreter::Visit(core::RegExpLiteral* regexp) {
  ctx_->Return(JSRegExp::New(regexp->value(), regexp->flags()));
}


void Interpreter::Visit(core::ArrayLiteral* literal) {
  // when in parse phase, have already removed last elision.
  JSArray* const ary = JSArray::New(ctx_);
  std::size_t current = 0;
  std::tr1::array<char, 30> buffer;
  BOOST_FOREACH(core::Expression* const expr, literal->items()) {
    if (expr) {
      EVAL(expr);
      const JSVal value = GetValue(ctx_->ret(), CHECK);
      std::snprintf(buffer.data(), buffer.size(),
                    Format<std::size_t>::printf, current);
      const Symbol index = ctx_->Intern(buffer.data());
      ary->DefineOwnProperty(
          ctx_, index,
          new DataDescriptor(value, PropertyDescriptor::WRITABLE |
                                    PropertyDescriptor::ENUMERABLE |
                                    PropertyDescriptor::CONFIGURABLE),
          false, CHECK);
    }
    ++current;
  }
  ary->Put(ctx_, ctx_->length_symbol(),
           JSVal(static_cast<double>(current)), false, CHECK);
  ctx_->Return(ary);
}


void Interpreter::Visit(core::ObjectLiteral* literal) {
  using std::tr1::get;
  using core::ObjectLiteral;
  JSObject* const obj = JSObject::New(ctx_);

  // section 11.1.5
  BOOST_FOREACH(const ObjectLiteral::Property& prop, literal->properties()) {
    const ObjectLiteral::PropertyDescriptorType type(get<0>(prop));
    const core::Identifier* const ident = get<1>(prop);
    const Symbol name = ctx_->Intern(ident->value());
    PropertyDescriptor* desc = NULL;
    if (type == ObjectLiteral::DATA) {
      EVAL(get<2>(prop));
      const JSVal value = GetValue(ctx_->ret(), CHECK);
      desc = new DataDescriptor(value,
                                PropertyDescriptor::WRITABLE |
                                PropertyDescriptor::ENUMERABLE |
                                PropertyDescriptor::CONFIGURABLE);
    } else {
      EVAL(get<2>(prop));
      if (type == ObjectLiteral::GET) {
        desc = new AccessorDescriptor(ctx_->ret().object(), NULL,
                                      PropertyDescriptor::ENUMERABLE |
                                      PropertyDescriptor::CONFIGURABLE);
      } else {
        desc = new AccessorDescriptor(NULL, ctx_->ret().object(),
                                      PropertyDescriptor::ENUMERABLE |
                                      PropertyDescriptor::CONFIGURABLE);
      }
    }
    // section 11.1.5 step 4
    // Syntax error detection is already passed in parser phase.
    // Because syntax error is early error (section 16 Errors)
    // syntax error is reported at parser phase.
    // So, in interpreter phase, there's nothing to do.
    obj->DefineOwnProperty(ctx_, name, desc, false, CHECK);
  }
  ctx_->Return(obj);
}


void Interpreter::Visit(core::FunctionLiteral* func) {
  ctx_->Return(
      JSCodeFunction::New(ctx_, func, ctx_->lexical_env()));
}


void Interpreter::Visit(core::IdentifierAccess* prop) {
  EVAL(prop->target());
  const JSVal base_value = GetValue(ctx_->ret(), CHECK);
  base_value.CheckObjectCoercible(CHECK);
  const Symbol sym = ctx_->Intern(prop->key()->value());
  ctx_->Return(
      JSReference::New(ctx_, base_value, sym, ctx_->IsStrict()));
}


void Interpreter::Visit(core::IndexAccess* prop) {
  EVAL(prop->target());
  const JSVal base_value = GetValue(ctx_->ret(), CHECK);
  EVAL(prop->key());
  const JSVal name_value = GetValue(ctx_->ret(), CHECK);
  base_value.CheckObjectCoercible(CHECK);
  const JSString* const name = name_value.ToString(ctx_, CHECK);
  ctx_->Return(
      JSReference::New(ctx_,
                       base_value,
                       ctx_->Intern(*name),
                       ctx_->IsStrict()));
}


void Interpreter::Visit(core::FunctionCall* call) {
  EVAL(call->target());
  const JSVal target = ctx_->ret();

  Arguments args(ctx_, call->args().size());
  std::size_t n = 0;
  BOOST_FOREACH(core::Expression* const expr, call->args()) {
    EVAL(expr);
    args[n++] = GetValue(ctx_->ret(), CHECK);
  }

  const JSVal func = GetValue(target, CHECK);
  if (!func.IsCallable()) {
    ctx_->set_error(JSErrorCode::TypeError);
    return;
  }
  if (target.IsReference()) {
    const JSReference* const ref = target.reference();
    if (ref->IsPropertyReference()) {
      args.set_this_binding(*(ref->base()));
    } else {
      assert(ref->base()->IsEnvironment());
      args.set_this_binding(ref->base()->environment()->ImplicitThisValue());
    }
  } else {
    args.set_this_binding(JSVal::Undefined());
  }

  ctx_->ret() = func.object()->AsCallable()->Call(args, CHECK);
}


void Interpreter::Visit(core::ConstructorCall* call) {
  EVAL(call->target());
  const JSVal target = ctx_->ret();

  Arguments args(ctx_, call->args().size());
  std::size_t n = 0;
  BOOST_FOREACH(core::Expression* const expr, call->args()) {
    EVAL(expr);
    args[n++] = GetValue(ctx_->ret(), CHECK);
  }

  const JSVal func = GetValue(target, CHECK);
  if (!func.IsCallable()) {
    ctx_->set_error(JSErrorCode::TypeError);
    return;
  }
  JSFunction* const constructor = func.object()->AsCallable();
  JSObject* const obj = JSObject::New(ctx_);
  const JSVal proto = constructor->Get(
      ctx_, ctx_->Intern("prototype"), CHECK);
  if (proto.IsObject()) {
    obj->set_prototype(proto.object());
  }
  args.set_this_binding(JSVal(obj));
  const JSVal result = constructor->Call(args, CHECK);
  if (result.IsObject()) {
    ctx_->ret() = result;
  } else {
    ctx_->Return(obj);
  }
}


// section 8.7.1 GetValue
JSVal Interpreter::GetValue(const JSVal& val, JSErrorCode::Type* error) {
  if (!val.IsReference()) {
    return val;
  }
  const JSReference* const ref = val.reference();
  const JSVal* const base = ref->base();
  if (ref->IsUnresolvableReference()) {
    *error = JSErrorCode::TypeError;
    return JSVal::Undefined();
  }
  if (ref->IsPropertyReference()) {
    if (ref->HasPrimitiveBase()) {
      // section 8.7.1 special [[Get]]
      const JSObject* const o = base->ToObject(ctx_, error);
      if (*error) {
        return JSVal::Undefined();
      }
      PropertyDescriptor* desc = o->GetProperty(ref->GetReferencedName());
      if (!desc) {
        return JSVal::Undefined();
      }
      if (desc->IsDataDescriptor()) {
        return desc->AsDataDescriptor()->value();
      } else {
        assert(desc->IsAccessorDescriptor());
        AccessorDescriptor* ac = desc->AsAccessorDescriptor();
        if (ac->get()) {
          JSVal res = ac->get()->AsCallable()->Call(Arguments(ctx_, *base),
                                                    error);
          if (*error) {
            return JSVal::Undefined();
          }
          return res;
        } else {
          return JSVal::Undefined();
        }
      }
    } else {
      JSVal res = base->object()->Get(ctx_,
                                      ref->GetReferencedName(), error);
      if (*error) {
        return JSVal::Undefined();
      }
      return res;
    }
    return JSVal::Undefined();
  } else {
    JSVal res = base->environment()->GetBindingValue(
        ctx_, ref->GetReferencedName(), ref->IsStrictReference(), error);
    if (*error) {
      return JSVal::Undefined();
    }
    return res;
  }
}


#define ERRCHECK  error);\
  if (*error) {\
    return;\
  }\
  ((void)0


// section 8.7.2 PutValue
void Interpreter::PutValue(const JSVal& val, const JSVal& w,
                           JSErrorCode::Type* error) {
  if (!val.IsReference()) {
    *error = JSErrorCode::ReferenceError;
    return;
  }
  const JSReference* const ref = val.reference();
  const JSVal* const base = ref->base();
  if (ref->IsUnresolvableReference()) {
    if (ref->IsStrictReference()) {
      *error = JSErrorCode::ReferenceError;
      return;
    }
    ctx_->global_obj()->Put(ctx_, ref->GetReferencedName(),
                            w, false, ERRCHECK);
  } else if (ref->IsPropertyReference()) {
    if (ref->HasPrimitiveBase()) {
      const Symbol sym = ref->GetReferencedName();
      const bool th = ref->IsStrictReference();
      JSObject* const o = base->ToObject(ctx_, ERRCHECK);
      if (!o->CanPut(sym)) {
        if (th) {
          *error = JSErrorCode::TypeError;
        }
        return;
      }
      PropertyDescriptor* const own_desc = o->GetOwnProperty(sym);
      if (own_desc && own_desc->IsDataDescriptor()) {
        if (th) {
          *error = JSErrorCode::TypeError;
        }
        return;
      }
      PropertyDescriptor* const desc = o->GetProperty(sym);
      if (desc && desc->IsAccessorDescriptor()) {
        AccessorDescriptor* ac = desc->AsAccessorDescriptor();
        assert(ac->set());
        ac->set()->AsCallable()->Call(Arguments(ctx_, *base), ERRCHECK);
      } else {
        if (th) {
          *error = JSErrorCode::TypeError;
        }
      }
      return;
    } else {
      base->object()->Put(ctx_, ref->GetReferencedName(), w,
                          ref->IsStrictReference(), ERRCHECK);
    }
  } else {
    assert(base->environment());
    base->environment()->SetMutableBinding(ctx_,
                                           ref->GetReferencedName(), w,
                                           ref->IsStrictReference(), ERRCHECK);
  }
}


#undef ERRCHECK


bool Interpreter::SameValue(const JSVal& lhs, const JSVal& rhs) {
  if (lhs.type() != rhs.type()) {
    return false;
  }
  if (lhs.IsUndefined()) {
    return true;
  }
  if (lhs.IsNull()) {
    return true;
  }
  if (lhs.IsNumber()) {
    // TODO(Constellation)
    // more exactly number comparison
    const double& lhsv = lhs.number();
    const double& rhsv = rhs.number();
    if (std::isnan(lhsv) && std::isnan(rhsv)) {
      return true;
    }
    if (lhsv == rhsv) {
      if (std::signbit(lhsv) && std::signbit(rhsv)) {
        return true;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }
  if (lhs.IsString()) {
    return *(lhs.string()) == *(rhs.string());
  }
  if (lhs.IsBoolean()) {
    return lhs.boolean() == rhs.boolean();
  }
  if (lhs.IsObject()) {
    return lhs.object() == rhs.object();
  }
  return false;
}


bool Interpreter::StrictEqual(const JSVal& lhs, const JSVal& rhs) {
  if (lhs.type() != rhs.type()) {
    return false;
  }
  if (lhs.IsUndefined()) {
    return true;
  }
  if (lhs.IsNull()) {
    return true;
  }
  if (lhs.IsNumber()) {
    const double& lhsv = lhs.number();
    const double& rhsv = rhs.number();
    if (std::isnan(lhsv) || std::isnan(rhsv)) {
      return false;
    }
    return lhsv == rhsv;
  }
  if (lhs.IsString()) {
    return *(lhs.string()) == *(rhs.string());
  }
  if (lhs.IsBoolean()) {
    return lhs.boolean() == rhs.boolean();
  }
  if (lhs.IsObject()) {
    return lhs.object() == rhs.object();
  }
  return false;
}


#define ABSTRACT_CHECK\
  CHECK_TO_WITH(error, false)


bool Interpreter::AbstractEqual(const JSVal& lhs, const JSVal& rhs,
                                JSErrorCode::Type* error) {
  if (lhs.type() == rhs.type()) {
    if (lhs.IsUndefined()) {
      return true;
    }
    if (lhs.IsNull()) {
      return true;
    }
    if (lhs.IsNumber()) {
      const double& lhsv = lhs.number();
      const double& rhsv = rhs.number();
      if (std::isnan(lhsv) || std::isnan(rhsv)) {
        return false;
      }
      return lhsv == rhsv;
    }
    if (lhs.IsString()) {
      return *(lhs.string()) == *(rhs.string());
    }
    if (lhs.IsBoolean()) {
      return lhs.boolean() == rhs.boolean();
    }
    if (lhs.IsObject()) {
      return lhs.object() == rhs.object();
    }
    return false;
  }
  if (lhs.IsNull() && rhs.IsUndefined()) {
    return true;
  }
  if (lhs.IsUndefined() && rhs.IsNull()) {
    return true;
  }
  if (lhs.IsNumber() && rhs.IsString()) {
    const double num = rhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(lhs, JSVal(num), error);
  }
  if (lhs.IsString() && rhs.IsNumber()) {
    const double num = lhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(JSVal(num), rhs, error);
  }
  if (lhs.IsBoolean()) {
    const double num = lhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(JSVal(num), rhs, error);
  }
  if (rhs.IsBoolean()) {
    const double num = rhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(lhs, JSVal(num), error);
  }
  if ((lhs.IsString() || lhs.IsNumber()) &&
      rhs.IsObject()) {
    const JSVal prim = rhs.ToPrimitive(ctx_,
                                       JSObject::NONE, ABSTRACT_CHECK);
    return AbstractEqual(lhs, prim, error);
  }
  if (lhs.IsObject() &&
      (rhs.IsString() || rhs.IsNumber())) {
    const JSVal prim = lhs.ToPrimitive(ctx_,
                                       JSObject::NONE, ABSTRACT_CHECK);
    return AbstractEqual(prim, rhs, error);
  }
  return false;
}
#undef ABSTRACT_CHECK


// section 11.8.5
#define LT_CHECK\
  CHECK_TO_WITH(error, CMP_ERROR)


Interpreter::CompareKind Interpreter::Compare(const JSVal& lhs,
                                              const JSVal& rhs,
                                              bool left_first,
                                              JSErrorCode::Type* error) {
  JSVal px;
  JSVal py;
  if (left_first) {
    px = lhs.ToPrimitive(ctx_, JSObject::NUMBER, LT_CHECK);
    py = rhs.ToPrimitive(ctx_, JSObject::NUMBER, LT_CHECK);
  } else {
    py = rhs.ToPrimitive(ctx_, JSObject::NUMBER, LT_CHECK);
    px = lhs.ToPrimitive(ctx_, JSObject::NUMBER, LT_CHECK);
  }
  if (px.IsString() && py.IsString()) {
    // step 4
    return (*(px.string()) < *(py.string())) ? CMP_TRUE : CMP_FALSE;
  } else {
    const double nx = px.ToNumber(ctx_, LT_CHECK);
    const double ny = py.ToNumber(ctx_, LT_CHECK);
    if (std::isnan(nx) || std::isnan(ny)) {
      return CMP_UNDEFINED;
    }
    if (nx == ny) {
      if (std::signbit(nx) != std::signbit(ny)) {
        return CMP_FALSE;
      }
      return CMP_FALSE;
    }
    if (nx == std::numeric_limits<double>::infinity()) {
      return CMP_FALSE;
    }
    if (ny == std::numeric_limits<double>::infinity()) {
      return CMP_TRUE;
    }
    if (ny == (-std::numeric_limits<double>::infinity())) {
      return CMP_FALSE;
    }
    if (nx == (-std::numeric_limits<double>::infinity())) {
      return CMP_TRUE;
    }
    return (nx < ny) ? CMP_TRUE : CMP_FALSE;
  }
}
#undef LT_CHECK


JSDeclEnv* Interpreter::NewDeclarativeEnvironment(Context* ctx, JSEnv* env) {
  return JSDeclEnv::New(ctx, env);
}


JSObjectEnv* Interpreter::NewObjectEnvironment(Context* ctx,
                                               JSObject* val, JSEnv* env) {
  assert(val);
  return JSObjectEnv::New(ctx, env, val);
}


JSReference* Interpreter::GetIdentifierReference(JSEnv* lex,
                                                 Symbol name, bool strict) {
  JSEnv* env = lex;
  while (env) {
    if (env->HasBinding(name)) {
      return JSReference::New(ctx_, JSVal(env), name, strict);
    } else {
      env = env->outer();
    }
  }
  return JSReference::New(ctx_, JSVal::Undefined(), name, strict);
}

#undef CHECK
#undef ERR_CHECK
#undef RETURN_STMT
#undef ABRUPT

} }  // namespace iv::lv5