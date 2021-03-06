#include <cstdio>
#include <cassert>
#include <cmath>
#include <iostream>  // NOLINT
#include <tr1/tuple>
#include <tr1/array>
#include <boost/foreach.hpp>
#include "token.h"
#include "hint.h"
#include "interpreter.h"
#include "jsreference.h"
#include "jsobject.h"
#include "jsstring.h"
#include "jsfunction.h"
#include "jsregexp.h"
#include "jserror.h"
#include "jsarguments.h"
#include "property.h"
#include "jsenv.h"
#include "jsarray.h"
#include "context.h"
#include "jsast.h"
#include "runtime_global.h"

namespace iv {
namespace lv5 {

#define CHECK  ctx_->error());\
  if (ctx_->IsError()) {\
    return;\
  }\
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY

#define CHECK_IN_STMT  ctx_->error());\
  if (ctx_->IsError()) {\
    RETURN_STMT(Context::THROW, JSUndefined, NULL);\
  }\
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY

#define CHECK_TO_WITH(error, val) error);\
  if (*error) {\
    return val;\
  }\
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY


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

#define EVAL_IN_STMT(node)\
  node->Accept(this);\
  if (ctx_->IsError()) {\
    RETURN_STMT(Context::THROW, JSUndefined, NULL);\
  }

Interpreter::ContextSwitcher::ContextSwitcher(Context* ctx,
                                              JSEnv* lex,
                                              JSEnv* var,
                                              const JSVal& binding,
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
    JSCodeFunction* code,
    const Arguments& args,
    Error* error) {
  // step 1
  JSVal this_value = args.this_binding();
  if (!code->IsStrict()) {
    if (this_value.IsUndefined()) {
      this_value.set_value(ctx_->global_obj());
    } else if (!this_value.IsObject()) {
      JSObject* const obj = this_value.ToObject(ctx_, CHECK_IN_STMT);
      this_value.set_value(obj);
    }
  }
  // section 10.5 Declaration Binding Instantiation
  const Scope& scope = code->code()->scope();

  // step 1
  JSDeclEnv* const env = NewDeclarativeEnvironment(ctx_, code->scope());
  const ContextSwitcher switcher(ctx_, env, env, this_value,
                                 code->IsStrict());

  // step 2
  const bool configurable_bindings = false;

  // step 4
  {
    const Arguments::JSVals& arguments = args.args();
    const std::size_t arg_count = arguments.size();
    std::size_t n = 0;
    BOOST_FOREACH(const Identifier* const ident,
                  code->code()->params()) {
      ++n;
      const Symbol arg_name = ctx_->Intern(*ident);
      if (!env->HasBinding(arg_name)) {
        env->CreateMutableBinding(ctx_, arg_name, configurable_bindings);
      }
      if (n > arg_count) {
        env->SetMutableBinding(ctx_, arg_name,
                               JSUndefined, ctx_->IsStrict(), CHECK_IN_STMT);
      } else {
        env->SetMutableBinding(ctx_, arg_name,
                               arguments[n-1], ctx_->IsStrict(), CHECK_IN_STMT);
      }
    }
  }

  // step 5
  BOOST_FOREACH(const FunctionLiteral* const f,
                scope.function_declarations()) {
    const Symbol fn = ctx_->Intern(*(f->name()));
    EVAL_IN_STMT(f);
    const JSVal fo = ctx_->ret();
    if (!env->HasBinding(fn)) {
      env->CreateMutableBinding(ctx_, fn, configurable_bindings);
    }
    env->SetMutableBinding(ctx_, fn, fo, ctx_->IsStrict(), CHECK_IN_STMT);
  }

  // step 6, 7
  // TODO(Constellation) code check (function)
  const Symbol arguments_symbol = ctx_->arguments_symbol();
  if (!env->HasBinding(arguments_symbol)) {
    JSArguments* const args_obj = JSArguments::New(ctx_,
                                                   code,
                                                   code->code()->params(),
                                                   args,
                                                   env,
                                                   ctx_->IsStrict());
    if (ctx_->IsStrict()) {
      env->CreateImmutableBinding(arguments_symbol);
      env->InitializeImmutableBinding(arguments_symbol, args_obj);
    } else {
      env->CreateMutableBinding(ctx_, ctx_->arguments_symbol(),
                                configurable_bindings);
      env->SetMutableBinding(ctx_, arguments_symbol,
                             args_obj, false, CHECK_IN_STMT);
    }
  }

  // step 8
  BOOST_FOREACH(const Scope::Variable& var, scope.variables()) {
    const Symbol dn = ctx_->Intern(*(var.first));
    if (!env->HasBinding(dn)) {
      env->CreateMutableBinding(ctx_, dn, configurable_bindings);
      env->SetMutableBinding(ctx_, dn,
                             JSUndefined, ctx_->IsStrict(), CHECK_IN_STMT);
    }
  }

  {
    const FunctionLiteral::DeclType type = code->code()->type();
    if (type == FunctionLiteral::STATEMENT ||
        (type == FunctionLiteral::EXPRESSION && code->name())) {
      const Symbol name = ctx_->Intern(*(code->name()));
      if (!env->HasBinding(name)) {
        env->CreateImmutableBinding(name);
        env->InitializeImmutableBinding(name, code);
      }
    }
  }

  BOOST_FOREACH(const Statement* const stmt, code->code()->body()) {
    EVAL_IN_STMT(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      RETURN_STMT(Context::THROW, ctx_->ret(), NULL);
    }
    if (ctx_->IsMode<Context::RETURN>()) {
      RETURN_STMT(Context::RETURN, ctx_->ret(), NULL);
    }
    assert(ctx_->IsMode<Context::NORMAL>());
  }
  RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
}


void Interpreter::Run(const FunctionLiteral* global, bool is_eval) {
  // section 10.5 Declaration Binding Instantiation
  const bool configurable_bindings = is_eval;
  const Scope& scope = global->scope();
  JSEnv* const env = ctx_->variable_env();
  const StrictSwitcher switcher(ctx_, global->strict());
  BOOST_FOREACH(const FunctionLiteral* const f,
                scope.function_declarations()) {
    const Symbol fn = ctx_->Intern(*(f->name()));
    EVAL_IN_STMT(f);
    JSVal fo = ctx_->ret();
    if (!env->HasBinding(fn)) {
      env->CreateMutableBinding(ctx_, fn, configurable_bindings);
    }
    env->SetMutableBinding(ctx_, fn, fo, ctx_->IsStrict(), CHECK_IN_STMT);
  }

  BOOST_FOREACH(const Scope::Variable& var, scope.variables()) {
    const Symbol dn = ctx_->Intern(*(var.first));
    if (!env->HasBinding(dn)) {
      env->CreateMutableBinding(ctx_, dn, configurable_bindings);
      env->SetMutableBinding(ctx_, dn,
                             JSUndefined, ctx_->IsStrict(), CHECK_IN_STMT);
    }
  }

  JSVal value;
  // section 14 Program
  BOOST_FOREACH(const Statement* const stmt, global->body()) {
    EVAL_IN_STMT(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      // section 12.1 step 4
      // TODO(Constellation) value to exception
      RETURN_STMT(Context::THROW, value, NULL);
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


void Interpreter::Visit(const Block* block) {
  // section 12.1 Block
  ctx_->set_mode(Context::NORMAL);
  JSVal value;
  BOOST_FOREACH(const Statement* const stmt, block->body()) {
    EVAL_IN_STMT(stmt);
    if (ctx_->IsMode<Context::THROW>()) {
      // section 12.1 step 4
      // TODO(Constellation) value to exception
      RETURN_STMT(Context::THROW, value, NULL);
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


void Interpreter::Visit(const FunctionStatement* stmt) {
  const FunctionLiteral* const func = stmt->function();
  Visit(func->name());
  const JSVal lhs = ctx_->ret();
  Visit(func);
  const JSVal val = GetValue(ctx_->ret(), CHECK_IN_STMT);
  PutValue(lhs, val, CHECK_IN_STMT);
}


void Interpreter::Visit(const VariableStatement* var) {
  // bool is_const = var->IsConst();
  BOOST_FOREACH(const Declaration* const decl, var->decls()) {
    Visit(decl->name());
    const JSVal lhs = ctx_->ret();
    if (decl->expr()) {
      EVAL_IN_STMT(decl->expr());
      const JSVal val = GetValue(ctx_->ret(), CHECK_IN_STMT);
      PutValue(lhs, val, CHECK_IN_STMT);
    }
  }
  RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
}


void Interpreter::Visit(const FunctionDeclaration* func) {
  RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
}


void Interpreter::Visit(const EmptyStatement* empty) {
  RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
}


void Interpreter::Visit(const IfStatement* stmt) {
  EVAL_IN_STMT(stmt->cond());
  const JSVal expr = GetValue(ctx_->ret(), CHECK_IN_STMT);
  const bool val = expr.ToBoolean(CHECK_IN_STMT);
  if (val) {
    EVAL_IN_STMT(stmt->then_statement());
    // through then statement's result
  } else {
    const Statement* const else_stmt = stmt->else_statement();
    if (else_stmt) {
      EVAL_IN_STMT(else_stmt);
      // through else statement's result
    } else {
      RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
    }
  }
}


void Interpreter::Visit(const DoWhileStatement* stmt) {
  JSVal value;
  bool iterating = true;
  while (iterating) {
    EVAL_IN_STMT(stmt->body());
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
    EVAL_IN_STMT(stmt->cond());
    const JSVal expr = GetValue(ctx_->ret(), CHECK_IN_STMT);
    const bool val = expr.ToBoolean(CHECK_IN_STMT);
    iterating = val;
  }
  RETURN_STMT(Context::NORMAL, value, NULL);
}


void Interpreter::Visit(const WhileStatement* stmt) {
  JSVal value;
  while (true) {
    EVAL_IN_STMT(stmt->cond());
    const JSVal expr = GetValue(ctx_->ret(), CHECK_IN_STMT);
    const bool val = expr.ToBoolean(CHECK_IN_STMT);
    if (val) {
      EVAL_IN_STMT(stmt->body());
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


void Interpreter::Visit(const ForStatement* stmt) {
  if (stmt->init()) {
    EVAL_IN_STMT(stmt->init());
    GetValue(ctx_->ret(), CHECK_IN_STMT);
  }
  JSVal value;
  while (true) {
    if (stmt->cond()) {
      EVAL_IN_STMT(stmt->cond());
      const JSVal expr = GetValue(ctx_->ret(), CHECK_IN_STMT);
      const bool val = expr.ToBoolean(CHECK_IN_STMT);
      if (!val) {
        RETURN_STMT(Context::NORMAL, value, NULL);
      }
    }
    EVAL_IN_STMT(stmt->body());
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
      EVAL_IN_STMT(stmt->next());
      GetValue(ctx_->ret(), CHECK_IN_STMT);
    }
  }
}


void Interpreter::Visit(const ForInStatement* stmt) {
  EVAL_IN_STMT(stmt->enumerable());
  const JSVal expr = GetValue(ctx_->ret(), CHECK_IN_STMT);
  if (expr.IsNull() || expr.IsUndefined()) {
    RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
  }
  JSObject* const obj = expr.ToObject(ctx_, CHECK_IN_STMT);
  JSVal value;
  JSObject* current = obj;
  do {
    BOOST_FOREACH(const JSObject::Properties::value_type& set,
                  current->table()) {
      if (!set.second.IsEnumerable()) {
        continue;
      }
      JSVal rhs(ctx_->ToString(set.first));
      EVAL_IN_STMT(stmt->each());
      if (stmt->each()->AsVariableStatement()) {
        const Identifier* const ident =
            stmt->each()->AsVariableStatement()->decls().front()->name();
        EVAL_IN_STMT(ident);
      }
      JSVal lhs = ctx_->ret();
      PutValue(lhs, rhs, CHECK_IN_STMT);
      EVAL_IN_STMT(stmt->body());
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


void Interpreter::Visit(const ContinueStatement* stmt) {
  RETURN_STMT(Context::CONTINUE, JSUndefined, stmt->target());
}


void Interpreter::Visit(const BreakStatement* stmt) {
  if (!stmt->target() && stmt->label()) {
    // interpret as EmptyStatement
    RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
  }
  RETURN_STMT(Context::BREAK, JSUndefined, stmt->target());
}


void Interpreter::Visit(const ReturnStatement* stmt) {
  if (stmt->expr()) {
    EVAL_IN_STMT(stmt->expr());
    const JSVal value = GetValue(ctx_->ret(), CHECK_IN_STMT);
    RETURN_STMT(Context::RETURN, value, NULL);
  } else {
    RETURN_STMT(Context::RETURN, JSUndefined, NULL);
  }
}


// section 12.10 The with Statement
void Interpreter::Visit(const WithStatement* stmt) {
  EVAL_IN_STMT(stmt->context());
  const JSVal val = GetValue(ctx_->ret(), CHECK_IN_STMT);
  JSObject* const obj = val.ToObject(ctx_, CHECK_IN_STMT);
  JSEnv* const old_env = ctx_->lexical_env();
  JSObjectEnv* const new_env = NewObjectEnvironment(ctx_, obj, old_env);
  new_env->set_provide_this(true);
  {
    const LexicalEnvSwitcher switcher(ctx_, new_env);
    EVAL_IN_STMT(stmt->body());  // RETURN_STMT is body's value
  }
}


void Interpreter::Visit(const LabelledStatement* stmt) {
  EVAL_IN_STMT(stmt->body());
}


void Interpreter::Visit(const SwitchStatement* stmt) {
  EVAL_IN_STMT(stmt->expr());
  const JSVal cond = GetValue(ctx_->ret(), CHECK_IN_STMT);
  // Case Block
  JSVal value;
  {
    typedef SwitchStatement::CaseClauses CaseClauses;
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
          EVAL_IN_STMT(clause->expr());
          const JSVal res = GetValue(ctx_->ret(), CHECK_IN_STMT);
          if (StrictEqual(cond, res)) {
            found = true;
          }
        }
      }
      // case's fall through
      if (found) {
        BOOST_FOREACH(const Statement* const st, clause->body()) {
          EVAL_IN_STMT(st);
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
        BOOST_FOREACH(const Statement* const st, (*it)->body()) {
          EVAL_IN_STMT(st);
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
void Interpreter::Visit(const ThrowStatement* stmt) {
  EVAL_IN_STMT(stmt->expr());
  const JSVal ref = GetValue(ctx_->ret(), CHECK_IN_STMT);
  ctx_->error()->Report(ref);
  RETURN_STMT(Context::THROW, ref, NULL);
}


// section 12.14 The try Statement
void Interpreter::Visit(const TryStatement* stmt) {
  stmt->body()->Accept(this);  // evaluate with no error check
  if (ctx_->IsMode<Context::THROW>() || ctx_->IsError()) {
    if (stmt->catch_block()) {
      const JSVal ex = ctx_->ErrorVal();
      ctx_->set_mode(Context::NORMAL);
      ctx_->error()->Clear();
      JSEnv* const old_env = ctx_->lexical_env();
      JSEnv* const catch_env = NewDeclarativeEnvironment(ctx_, old_env);
      const Symbol name = ctx_->Intern(*(stmt->catch_name()));
      catch_env->CreateMutableBinding(ctx_, name, false);
      catch_env->SetMutableBinding(ctx_, name, ex, false, CHECK_IN_STMT);
      {
        const LexicalEnvSwitcher switcher(ctx_, catch_env);
        // evaluate with no error check (finally)
        stmt->catch_block()->Accept(this);
      }
    }
  }

  if (stmt->finally_block()) {
    const Context::Mode mode = ctx_->mode();
    const JSVal value = ctx_->ret();
    const BreakableStatement* const target = ctx_->target();

    ctx_->error()->Clear();
    ctx_->SetStatement(Context::Context::NORMAL, JSUndefined, NULL);
    stmt->finally_block()->Accept(this);
    if (ctx_->IsMode<Context::NORMAL>()) {
      if (mode == Context::THROW) {
        ctx_->error()->Report(value);
      }
      RETURN_STMT(mode, value, target);
    }
  }
}


void Interpreter::Visit(const DebuggerStatement* stmt) {
  // section 12.15 debugger statement
  // implementation define debugging facility is not available
  RETURN_STMT(Context::NORMAL, JSUndefined, NULL);
}


void Interpreter::Visit(const ExpressionStatement* stmt) {
  EVAL_IN_STMT(stmt->expr());
  const JSVal value = GetValue(ctx_->ret(), CHECK);
  RETURN_STMT(Context::NORMAL, value, NULL);
}


void Interpreter::Visit(const Assignment* assign) {
  using core::Token;
  EVAL(assign->left());
  const JSVal lref(ctx_->ret());
  JSVal result;
  if (assign->op() == Token::ASSIGN) {  // =
    EVAL(assign->right());
    const JSVal rhs = GetValue(ctx_->ret(), CHECK);
    result = rhs;
  } else {
    const JSVal lhs = GetValue(lref, CHECK);
    EVAL(assign->right());
    const JSVal rhs = GetValue(ctx_->ret(), CHECK);
    switch (assign->op()) {
      case Token::ASSIGN_ADD: {  // +=
        const JSVal lprim = lhs.ToPrimitive(ctx_, Hint::NONE, CHECK);
        const JSVal rprim = rhs.ToPrimitive(ctx_, Hint::NONE, CHECK);
        if (lprim.IsString() || rprim.IsString()) {
          const JSString* const lstr = lprim.ToString(ctx_, CHECK);
          const JSString* const rstr = rprim.ToString(ctx_, CHECK);
          result.set_value(JSString::New(ctx_, lstr->value(), rstr->value()));
          break;
        }
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
        result.set_value(core::DoubleToInt32(left_num)
                         >> (core::DoubleToInt32(right_num) & 0x1f));
        break;
      }
      case Token::ASSIGN_SHR: {  // >>>=
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        const uint32_t res = core::DoubleToUInt32(left_num)
            >> (core::DoubleToInt32(right_num) & 0x1f);
        result.set_value(res);
        break;
      }
      case Token::ASSIGN_SHL: {  // <<=
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        result.set_value(core::DoubleToInt32(left_num)
                         << (core::DoubleToInt32(right_num) & 0x1f));
        break;
      }
      case Token::ASSIGN_BIT_AND: {  // &=
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        result.set_value(core::DoubleToInt32(left_num)
                         & (core::DoubleToInt32(right_num)));
        break;
      }
      case Token::ASSIGN_BIT_OR: {  // |=
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        result.set_value(core::DoubleToInt32(left_num)
                         | (core::DoubleToInt32(right_num)));
        break;
      }
      case Token::ASSIGN_BIT_XOR: {  // ^=
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        result.set_value(core::DoubleToInt32(left_num)
                         ^ (core::DoubleToInt32(right_num)));
        break;
      }
      default: {
        UNREACHABLE();
        break;
      }
    }
  }
  // when parser find eval / arguments identifier in strict code,
  // parser raise "SyntaxError".
  // so, this path is not used in interpreter. (section 11.13.1 step4)
  PutValue(lref, result, CHECK);
  ctx_->Return(result);
}


void Interpreter::Visit(const BinaryOperation* binary) {
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
        const JSVal lprim = lhs.ToPrimitive(ctx_, Hint::NONE, CHECK);
        const JSVal rprim = rhs.ToPrimitive(ctx_, Hint::NONE, CHECK);
        if (lprim.IsString() || rprim.IsString()) {
          const JSString* const lstr = lprim.ToString(ctx_, CHECK);
          const JSString* const rstr = rprim.ToString(ctx_, CHECK);
          ctx_->Return(JSString::New(ctx_,
                                     lstr->value(), rstr->value()));
          return;
        }
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
        ctx_->Return(core::DoubleToInt32(left_num)
                     << (core::DoubleToInt32(right_num) & 0x1f));
        return;
      }

      case Token::SAR: {  // >>
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(core::DoubleToInt32(left_num)
                     >> (core::DoubleToInt32(right_num) & 0x1f));
        return;
      }

      case Token::SHR: {  // >>>
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        const uint32_t res = core::DoubleToUInt32(left_num)
            >> (core::DoubleToInt32(right_num) & 0x1f);
        ctx_->Return(res);
        return;
      }

      case Token::LT: {  // <
        const CompareKind res = Compare(lhs, rhs, CHECK);
        if (res == CMP_TRUE) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::GT: {  // >
        const CompareKind res = Compare(rhs, lhs, CHECK);
        if (res == CMP_TRUE) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::LTE: {  // <=
        const CompareKind res = Compare(rhs, lhs, CHECK);
        if (res == CMP_FALSE) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::GTE: {  // >=
        const CompareKind res = Compare(lhs, rhs, CHECK);
        if (res == CMP_FALSE) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::INSTANCEOF: {  // instanceof
        if (!rhs.IsObject()) {
          ctx_->error()->Report(Error::Type, "instanceof requires object");
          return;
        }
        JSObject* const robj = rhs.object();
        if (!robj->IsCallable()) {
          ctx_->error()->Report(Error::Type, "instanceof requires constructor");
          return;
        }
        const bool res = robj->AsCallable()->HasInstance(ctx_, lhs, CHECK);
        if (res) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::IN: {  // in
        if (!rhs.IsObject()) {
          ctx_->error()->Report(Error::Type, "in requires object");
          return;
        }
        const JSString* const name = lhs.ToString(ctx_, CHECK);
        const bool res = rhs.object()->HasProperty(ctx_->Intern(name->value()));
        if (res) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::EQ: {  // ==
        const bool res = AbstractEqual(lhs, rhs, CHECK);
        if (res) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::NE: {  // !=
        const bool res = AbstractEqual(lhs, rhs, CHECK);
        if (!res) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::EQ_STRICT: {  // ===
        if (StrictEqual(lhs, rhs)) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      case Token::NE_STRICT: {  // !==
        if (!StrictEqual(lhs, rhs)) {
          ctx_->Return(JSTrue);
        } else {
          ctx_->Return(JSFalse);
        }
        return;
      }

      // bitwise op
      case Token::BIT_AND: {  // &
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(core::DoubleToInt32(left_num)
                     & (core::DoubleToInt32(right_num)));
        return;
      }

      case Token::BIT_XOR: {  // ^
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(core::DoubleToInt32(left_num)
                     ^ (core::DoubleToInt32(right_num)));
        return;
      }

      case Token::BIT_OR: {  // |
        const double left_num = lhs.ToNumber(ctx_, CHECK);
        const double right_num = rhs.ToNumber(ctx_, CHECK);
        ctx_->Return(core::DoubleToInt32(left_num)
                     | (core::DoubleToInt32(right_num)));
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


void Interpreter::Visit(const ConditionalExpression* cond) {
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


void Interpreter::Visit(const UnaryOperation* unary) {
  using core::Token;
  switch (unary->op()) {
    case Token::DELETE: {
      EVAL(unary->expr());
      if (!ctx_->ret().IsReference()) {
        ctx_->Return(JSTrue);
        return;
      }
      // UnresolvableReference / EnvironmentReference is always created
      // by Identifier. and Identifier doesn't create PropertyReference.
      // so, in parser, when "delete Identifier" is found in strict code,
      // parser raise SyntaxError.
      // so, this path (section 11.4.1 step 3.a, step 5.a) is not used.
      const JSReference* const ref = ctx_->ret().reference();
      if (ref->IsUnresolvableReference()) {
        ctx_->Return(JSTrue);
        return;
      } else if (ref->IsPropertyReference()) {
        JSObject* const obj = ref->base()->ToObject(ctx_, CHECK);
        const bool result = obj->Delete(ref->GetReferencedName(),
                                        ref->IsStrictReference(), CHECK);
        ctx_->Return(JSVal::Bool(result));
      } else {
        assert(ref->base()->IsEnvironment());
        const bool res = ref->base()->environment()->DeleteBinding(
            ref->GetReferencedName());
        ctx_->Return(JSVal::Bool(res));
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
      // when parser find eval / arguments identifier in strict code,
      // parser raise "SyntaxError".
      // so, this path is not used in interpreter. (section 11.4.4 step2)
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
      // when parser find eval / arguments identifier in strict code,
      // parser raise "SyntaxError".
      // so, this path is not used in interpreter. (section 11.4.5 step2)
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
      ctx_->Return(~core::DoubleToInt32(value));
      return;
    }

    case Token::NOT: {
      EVAL(unary->expr());
      const JSVal expr = GetValue(ctx_->ret(), CHECK);
      const bool value = expr.ToBoolean(CHECK);
      if (!value) {
        ctx_->Return(JSTrue);
      } else {
        ctx_->Return(JSFalse);
      }
      return;
    }

    default:
      UNREACHABLE();
  }
}


void Interpreter::Visit(const PostfixExpression* postfix) {
  EVAL(postfix->expr());
  const JSVal lref = ctx_->ret();
  // when parser find eval / arguments identifier in strict code,
  // parser raise "SyntaxError".
  // so, this path is not used in interpreter.
  // (section 11.3.1 step2, 11.3.2 step2)
  const JSVal old = GetValue(lref, CHECK);
  const double value = old.ToNumber(ctx_, CHECK);
  const double new_value = value +
      ((postfix->op() == core::Token::INC) ? 1 : -1);
  PutValue(lref, new_value, CHECK);
  ctx_->Return(value);
}


void Interpreter::Visit(const StringLiteral* str) {
  ctx_->Return(JSString::New(ctx_, str->value()));
}


void Interpreter::Visit(const NumberLiteral* num) {
  ctx_->Return(num->value());
}


void Interpreter::Visit(const Identifier* ident) {
  // section 10.3.1 Identifier Resolution
  JSEnv* const env = ctx_->lexical_env();
  ctx_->Return(
      GetIdentifierReference(env,
                             ctx_->Intern(*ident),
                             ctx_->IsStrict()));
}


void Interpreter::Visit(const ThisLiteral* literal) {
  ctx_->Return(ctx_->this_binding());
}


void Interpreter::Visit(const NullLiteral* lit) {
  ctx_->ret().set_null();
}


void Interpreter::Visit(const TrueLiteral* lit) {
  ctx_->Return(JSTrue);
}


void Interpreter::Visit(const FalseLiteral* lit) {
  ctx_->Return(JSFalse);
}


void Interpreter::Visit(const Undefined* lit) {
  ctx_->ret().set_undefined();
}


void Interpreter::Visit(const RegExpLiteral* regexp) {
  ctx_->Return(JSRegExp::New(regexp->regexp()));
}


void Interpreter::Visit(const ArrayLiteral* literal) {
  // when in parse phase, have already removed last elision.
  JSArray* const ary = JSArray::New(ctx_);
  std::size_t current = 0;
  std::tr1::array<char, 30> buffer;
  BOOST_FOREACH(const Expression* const expr, literal->items()) {
    if (expr) {
      EVAL(expr);
      const JSVal value = GetValue(ctx_->ret(), CHECK);
      std::snprintf(buffer.data(), buffer.size(),
                    "%lu",
                    static_cast<unsigned long>(current));  // NOLINT
      const Symbol index = ctx_->Intern(buffer.data());
      ary->DefineOwnProperty(
          ctx_, index,
          DataDescriptor(value, PropertyDescriptor::WRITABLE |
                                PropertyDescriptor::ENUMERABLE |
                                PropertyDescriptor::CONFIGURABLE),
          false, CHECK);
    }
    ++current;
  }
  ary->Put(ctx_, ctx_->length_symbol(),
           current, false, CHECK);
  ctx_->Return(ary);
}


void Interpreter::Visit(const ObjectLiteral* literal) {
  using std::tr1::get;
  JSObject* const obj = JSObject::New(ctx_);

  // section 11.1.5
  BOOST_FOREACH(const ObjectLiteral::Property& prop, literal->properties()) {
    const ObjectLiteral::PropertyDescriptorType type(get<0>(prop));
    const Identifier* const ident = get<1>(prop);
    const Symbol name = ctx_->Intern(*(ident));
    PropertyDescriptor desc;
    if (type == ObjectLiteral::DATA) {
      EVAL(get<2>(prop));
      const JSVal value = GetValue(ctx_->ret(), CHECK);
      desc = DataDescriptor(value,
                            PropertyDescriptor::WRITABLE |
                            PropertyDescriptor::ENUMERABLE |
                            PropertyDescriptor::CONFIGURABLE);
    } else {
      EVAL(get<2>(prop));
      if (type == ObjectLiteral::GET) {
        desc = AccessorDescriptor(ctx_->ret().object(), NULL,
                                  PropertyDescriptor::ENUMERABLE |
                                  PropertyDescriptor::CONFIGURABLE);
      } else {
        desc = AccessorDescriptor(NULL, ctx_->ret().object(),
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


void Interpreter::Visit(const FunctionLiteral* func) {
  ctx_->Return(
      JSCodeFunction::New(ctx_, func,
                          ctx_->current_script(), ctx_->lexical_env()));
}


void Interpreter::Visit(const IdentifierAccess* prop) {
  EVAL(prop->target());
  const JSVal base_value = GetValue(ctx_->ret(), CHECK);
  base_value.CheckObjectCoercible(CHECK);
  const Symbol sym = ctx_->Intern(*(prop->key()));
  ctx_->Return(
      JSReference::New(ctx_, base_value, sym, ctx_->IsStrict()));
}


void Interpreter::Visit(const IndexAccess* prop) {
  EVAL(prop->target());
  const JSVal base_value = GetValue(ctx_->ret(), CHECK);
  EVAL(prop->key());
  const JSVal name_value = GetValue(ctx_->ret(), CHECK);
  base_value.CheckObjectCoercible(CHECK);
  const JSString* const name = name_value.ToString(ctx_, CHECK);
  ctx_->Return(
      JSReference::New(ctx_,
                       base_value,
                       ctx_->Intern(name->value()),
                       ctx_->IsStrict()));
}


void Interpreter::Visit(const FunctionCall* call) {
  EVAL(call->target());
  const JSVal target = ctx_->ret();

  Arguments args(ctx_, call->args().size());
  std::size_t n = 0;
  BOOST_FOREACH(const Expression* const expr, call->args()) {
    EVAL(expr);
    args[n++] = GetValue(ctx_->ret(), CHECK);
  }

  const JSVal func = GetValue(target, CHECK);
  if (!func.IsCallable()) {
    ctx_->error()->Report(Error::Type, "not callable object");
    return;
  }
  if (target.IsReference()) {
    const JSReference* const ref = target.reference();
    if (ref->IsPropertyReference()) {
      args.set_this_binding(*(ref->base()));
    } else {
      assert(ref->base()->IsEnvironment());
      args.set_this_binding(ref->base()->environment()->ImplicitThisValue());
      // direct call to eval check
      {
        const JSNativeFunction* const native =
            func.object()->AsCallable()->AsNativeFunction();
        if (native &&
            native->function() == &runtime::GlobalEval) {
          // this function is eval function
          const Identifier* const maybe_eval = call->target()->AsIdentifier();
          if (maybe_eval &&
              maybe_eval->symbol() == ctx_->eval_symbol()) {
            // direct call to eval point
            ctx_->ret() = runtime::DirectCallToEval(args, CHECK);
            return;
          }
        }
      }
    }
  } else {
    args.set_this_binding(JSUndefined);
  }

  ctx_->ret() = func.object()->AsCallable()->Call(args, CHECK);
}


void Interpreter::Visit(const ConstructorCall* call) {
  EVAL(call->target());
  const JSVal target = ctx_->ret();

  Arguments args(ctx_, call->args().size());
  args.set_constructor_call(true);
  std::size_t n = 0;
  BOOST_FOREACH(const Expression* const expr, call->args()) {
    EVAL(expr);
    args[n++] = GetValue(ctx_->ret(), CHECK);
  }

  const JSVal func = GetValue(target, CHECK);
  if (!func.IsCallable()) {
    ctx_->error()->Report(Error::Type, "not callable object");
    return;
  }
  JSFunction* const constructor = func.object()->AsCallable();
  JSObject* const obj = JSObject::New(ctx_);
  const JSVal proto = constructor->Get(
      ctx_, ctx_->Intern("prototype"), CHECK);
  if (proto.IsObject()) {
    obj->set_prototype(proto.object());
  }
  args.set_this_binding(obj);
  const JSVal result = constructor->Call(args, CHECK);
  if (result.IsObject()) {
    ctx_->ret() = result;
  } else {
    ctx_->Return(obj);
  }
}


// section 8.7.1 GetValue
JSVal Interpreter::GetValue(const JSVal& val, Error* error) {
  if (!val.IsReference()) {
    return val;
  }
  const JSReference* const ref = val.reference();
  const JSVal* const base = ref->base();
  if (ref->IsUnresolvableReference()) {
    // TODO(Constellation) add symbol name
    error->Report(Error::Reference, "not defined");
    return JSUndefined;
  }
  if (ref->IsPropertyReference()) {
    if (ref->HasPrimitiveBase()) {
      // section 8.7.1 special [[Get]]
      const JSObject* const o = base->ToObject(ctx_, error);
      if (*error) {
        return JSUndefined;
      }
      const PropertyDescriptor desc = o->GetProperty(ref->GetReferencedName());
      if (desc.IsEmpty()) {
        return JSUndefined;
      }
      if (desc.IsDataDescriptor()) {
        return desc.AsDataDescriptor()->data();
      } else {
        assert(desc.IsAccessorDescriptor());
        const AccessorDescriptor* const ac = desc.AsAccessorDescriptor();
        if (ac->get()) {
          const JSVal res = ac->get()->AsCallable()->Call(
              Arguments(ctx_, *base), error);
          if (*error) {
            return JSUndefined;
          }
          return res;
        } else {
          return JSUndefined;
        }
      }
    } else {
      const JSVal res = base->object()->Get(ctx_,
                                            ref->GetReferencedName(), error);
      if (*error) {
        return JSUndefined;
      }
      return res;
    }
    return JSUndefined;
  } else {
    const JSVal res = base->environment()->GetBindingValue(
        ctx_, ref->GetReferencedName(), ref->IsStrictReference(), error);
    if (*error) {
      return JSUndefined;
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
                           Error* error) {
  if (!val.IsReference()) {
    error->Report(Error::Reference,
                  "target is not reference");
    return;
  }
  const JSReference* const ref = val.reference();
  const JSVal* const base = ref->base();
  if (ref->IsUnresolvableReference()) {
    if (ref->IsStrictReference()) {
      error->Report(Error::Reference,
                    "putting to unresolvable reference "
                    "not allowed in strict reference");
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
          error->Report(Error::Type, "cannot put value to object");
        }
        return;
      }
      const PropertyDescriptor own_desc = o->GetOwnProperty(sym);
      if (!own_desc.IsEmpty() && own_desc.IsDataDescriptor()) {
        if (th) {
          // TODO(Constellation) add symbol name
          error->Report(Error::Type,
                        "value to symbol defined and not data descriptor");
        }
        return;
      }
      const PropertyDescriptor desc = o->GetProperty(sym);
      if (!desc.IsEmpty() && desc.IsAccessorDescriptor()) {
        const AccessorDescriptor* const ac = desc.AsAccessorDescriptor();
        assert(ac->set());
        ac->set()->AsCallable()->Call(Arguments(ctx_, *base), ERRCHECK);
      } else {
        if (th) {
          error->Report(Error::Type, "value to symbol in transient object");
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
                                Error* error) {
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
    return AbstractEqual(lhs, num, error);
  }
  if (lhs.IsString() && rhs.IsNumber()) {
    const double num = lhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(num, rhs, error);
  }
  if (lhs.IsBoolean()) {
    const double num = lhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(num, rhs, error);
  }
  if (rhs.IsBoolean()) {
    const double num = rhs.ToNumber(ctx_, ABSTRACT_CHECK);
    return AbstractEqual(lhs, num, error);
  }
  if ((lhs.IsString() || lhs.IsNumber()) &&
      rhs.IsObject()) {
    const JSVal prim = rhs.ToPrimitive(ctx_,
                                       Hint::NONE, ABSTRACT_CHECK);
    return AbstractEqual(lhs, prim, error);
  }
  if (lhs.IsObject() &&
      (rhs.IsString() || rhs.IsNumber())) {
    const JSVal prim = lhs.ToPrimitive(ctx_,
                                       Hint::NONE, ABSTRACT_CHECK);
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
                                              Error* error) {
  const JSVal px = lhs.ToPrimitive(ctx_, Hint::NUMBER, LT_CHECK);
  const JSVal py = rhs.ToPrimitive(ctx_, Hint::NUMBER, LT_CHECK);
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
      return JSReference::New(ctx_, env, name, strict);
    } else {
      env = env->outer();
    }
  }
  return JSReference::New(ctx_, JSUndefined, name, strict);
}

#undef CHECK
#undef ERR_CHECK
#undef RETURN_STMT
#undef ABRUPT

} }  // namespace iv::lv5
