#include "cstring"
#include "ustring.h"
#include "arguments.h"
#include "context.h"
#include "interpreter.h"
#include "jsproperty.h"
#include "class.h"
#include "runtime.h"
namespace iv {
namespace lv5 {
namespace {

const std::string length_string("length");
const std::string eval_string("eval");
const std::string arguments_string("arguments");
const std::string caller_string("caller");
const std::string callee_string("callee");

}  // namespace

Context::Context()
  : global_obj_(),
    lexical_env_(NULL),
    variable_env_(NULL),
    binding_(&global_obj_),
    table_(),
    interp_(),
    mode_(NORMAL),
    ret_(),
    target_(NULL),
    error_(JSErrorCode::Normal),
    builtins_(),
    strict_(false),
    random_engine_(random_engine_type(),
                   random_distribution_type(0, 1)),
    length_symbol_(Intern(length_string)),
    eval_symbol_(Intern(eval_string)),
    arguments_symbol_(Intern(arguments_string)),
    caller_symbol_(Intern(caller_string)),
    callee_symbol_(Intern(callee_string)) {
  JSEnv* env = Interpreter::NewObjectEnvironment(this, &global_obj_, NULL);
  lexical_env_ = env;
  variable_env_ = env;
  interp_.set_context(this);
  Initialize();
}

Symbol Context::Intern(const core::StringPiece& str) {
  return table_.Lookup(str);
}

Symbol Context::Intern(const core::UStringPiece& str) {
  return table_.Lookup(str);
}

double Context::Random() {
  return random_engine_();
}

JSString* Context::ToString(Symbol sym) {
  return table_.ToString(this, sym);
}

bool Context::InCurrentLabelSet(
    const core::AnonymousBreakableStatement* stmt) const {
  // AnonymousBreakableStatement has empty label at first
  return !target_ || stmt == target_;
}

bool Context::InCurrentLabelSet(
    const core::NamedOnlyBreakableStatement* stmt) const {
  return stmt == target_;
}

void Context::Run(core::FunctionLiteral* global) {
  interp_.Run(global);
}

void Context::Initialize() {
  // Object
  JSNativeFunction* const obj_constructor =
      JSNativeFunction::New(this, &Runtime_ObjectConstructor);
  JSObject* const obj_proto = JSObject::NewPlain(this);
  obj_proto->set_cls(JSString::NewAsciiString(this, "Object"));

  struct Class obj_cls = {
    JSString::NewAsciiString(this, "Object"),
    obj_constructor,
    obj_proto
  };
  obj_proto->set_cls(obj_cls.name);

  // Function
  JSObject* const func_proto = JSObject::NewPlain(this);
  func_proto->set_prototype(obj_proto);
  struct Class func_cls = {
    JSString::NewAsciiString(this, "Function"),
    NULL,
    func_proto
  };
  func_proto->set_cls(func_cls.name);
  const Symbol func_name = Intern("Function");
  builtins_[func_name] = func_cls;
  {
    JSNativeFunction* const func =
        JSNativeFunction::New(this, &Runtime_FunctionToString);
    func_proto->DefineOwnProperty(
        this, Intern("toString"),
        new DataDescriptor(JSVal(func),
                           PropertyDescriptor::WRITABLE |
                           PropertyDescriptor::ENUMERABLE),
        false, NULL);
  }

  {
    // Object Define
    const Symbol name = Intern("Object");
    {
      JSNativeFunction* const func =
          JSNativeFunction::New(this, &Runtime_ObjectHasOwnProperty);
      obj_proto->DefineOwnProperty(
          this, Intern("hasOwnProperty"),
          new DataDescriptor(JSVal(func),
                             PropertyDescriptor::WRITABLE |
                             PropertyDescriptor::ENUMERABLE),
          false, NULL);
    }
    {
      JSNativeFunction* const func =
          JSNativeFunction::New(this, &Runtime_ObjectToString);
      obj_proto->DefineOwnProperty(
          this, Intern("toString"),
          new DataDescriptor(JSVal(func),
                             PropertyDescriptor::WRITABLE |
                             PropertyDescriptor::ENUMERABLE),
          false, NULL);
    }
    builtins_[name] = obj_cls;

    obj_constructor->DefineOwnProperty(
        this, Intern("prototype"),
        new DataDescriptor(JSVal(obj_proto), PropertyDescriptor::NONE),
        false, NULL);

    variable_env_->CreateMutableBinding(this, name, false);
    variable_env_->SetMutableBinding(this, name,
                                     JSVal(obj_constructor), strict_, NULL);
  }
  {
    // Array
    JSObject* const proto = JSObject::NewPlain(this);
    proto->set_prototype(obj_proto);
    struct Class cls = {
      JSString::NewAsciiString(this, "Array"),
      NULL,
      proto
    };
    proto->set_cls(cls.name);
    const Symbol name = Intern("Array");
    builtins_[name] = cls;
  }
  global_obj_.set_cls(JSString::NewAsciiString(this, "global"));
  global_obj_.set_prototype(obj_proto);
}

const Class& Context::Cls(Symbol name) {
  return builtins_[name];
}

const Class& Context::Cls(const core::StringPiece& str) {
  return builtins_[Intern(str)];
}

} }  // namespace iv::lv5