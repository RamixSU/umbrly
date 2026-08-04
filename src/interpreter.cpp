#include "interpreter.h"

#include <cctype>
#include <iostream>
#include <unordered_set>

#include "errors.h"

namespace umbrly {

struct FastIntProgram {
    enum class Op { Const, Local, StoreLocal, Add, Sub, Mul, Mod, Eq, Ne, Lt, Gt, Le, Ge,
                    JumpFalse, Jump, ForInit, ForNext, CallSelf, CallNamed, Return };
    struct Instr { Op op; long long value = 0; int arg = 0; int arg2 = 0; std::string text; };
    std::vector<Instr> code;
    size_t paramCount = 0;
    size_t localCount = 0;
    size_t loopCount = 0;
};

struct FastValueProgram {
    enum class Op { Const, Local, Global, StoreLocal, StoreGlobal, IndexSetLocal, IndexSetGlobal, Add, Sub, Mul, Div, Mod,
                    Eq, Ne, Lt, Gt, Le, Ge, Neg, Not, ToBool, MakeArray, IndexGet,
                    MemberGet, MemberSet, Call, CallMethod, Print, PauseInput, ReadInputLocal, ReadInputGlobal,
                    Pop, JumpFalse, Jump,
                    ForInit, ForNext, ForInitGlobal, ForNextGlobal, PushHandler, PopHandler,
                    ExecLoopStmt, Return };
    struct Instr {
        Op op;
        Value value;
        std::string text;
        int arg = 0;
        int line = 0;
        int arg2 = 0;
        const Stmt* stmt = nullptr;
    };
    std::vector<Instr> code;
    std::vector<std::string> localNames;
    std::vector<Type> localTypes;
    size_t paramCount = 0;
    size_t loopCount = 0;
};

namespace {

thread_local const std::unordered_set<std::string>* gKnownFastIntFunctions = nullptr;

bool compileValueExpr(const Expr* e, const std::unordered_map<std::string,int>& locals,
                      std::vector<FastValueProgram::Instr>& out) {
    using Op=FastValueProgram::Op;
    auto emitConst=[&](Value v){out.push_back({Op::Const,std::move(v),"",0,e->line});};
    if(auto* v=dynamic_cast<const IntLitExpr*>(e)){emitConst(Value::Int(v->value));return true;}
    if(auto* v=dynamic_cast<const FloatLitExpr*>(e)){emitConst(Value::Float(v->value));return true;}
    if(auto* v=dynamic_cast<const BoolLitExpr*>(e)){emitConst(Value::Bool(v->value));return true;}
    if(dynamic_cast<const NilLitExpr*>(e)){emitConst(Value::Nil());return true;}
    if(auto* v=dynamic_cast<const NameExpr*>(e)){
        auto it=locals.find(v->name);
        if(it!=locals.end())out.push_back({Op::Local,{},"",it->second,e->line});
        else out.push_back({Op::Global,{},v->name,0,e->line});
        return true;
    }
    if(auto* v=dynamic_cast<const StringLitExpr*>(e)){
        emitConst(Value::Str(""));
        for(const auto& seg:v->segments){
            if(seg.expr){if(!compileValueExpr(seg.expr.get(),locals,out))return false;}
            else out.push_back({Op::Const,Value::Str(seg.literal),"",0,e->line});
            out.push_back({Op::Add,{},"",0,e->line});
        }
        return true;
    }
    if(auto* v=dynamic_cast<const ArrayLitExpr*>(e)){
        for(const auto& item:v->items)if(!compileValueExpr(item.get(),locals,out))return false;
        out.push_back({Op::MakeArray,{},"",(int)v->items.size(),e->line});return true;
    }
    if(auto* v=dynamic_cast<const IndexExpr*>(e)){
        if(!compileValueExpr(v->base.get(),locals,out)||!compileValueExpr(v->index.get(),locals,out))return false;
        out.push_back({Op::IndexGet,{},"",0,e->line});return true;
    }
    if(auto* v=dynamic_cast<const CallExpr*>(e)){
        for(const auto& a:v->args)if(!compileValueExpr(a.get(),locals,out))return false;
        out.push_back({Op::Call,{},v->name,(int)v->args.size(),e->line});return true;
    }
    if(auto* v=dynamic_cast<const MemberExpr*>(e)){
        if(!compileValueExpr(v->base.get(),locals,out))return false;
        out.push_back({Op::MemberGet,{},v->name,0,e->line});return true;
    }
    if(auto* v=dynamic_cast<const MethodCallExpr*>(e)){
        if(!compileValueExpr(v->base.get(),locals,out))return false;
        for(const auto& a:v->args)if(!compileValueExpr(a.get(),locals,out))return false;
        out.push_back({Op::CallMethod,{},v->method,(int)v->args.size(),e->line});return true;
    }
    if(auto* v=dynamic_cast<const UnaryExpr*>(e)){
        if(!compileValueExpr(v->operand.get(),locals,out))return false;
        if(v->op=="-")out.push_back({Op::Neg,{},"",0,e->line});
        else if(v->op=="NOT")out.push_back({Op::Not,{},"",0,e->line});else return false;
        return true;
    }
    auto* b=dynamic_cast<const BinaryExpr*>(e);
    if(!b)return false;
    if(b->op=="AND"){
        if(!compileValueExpr(b->lhs.get(),locals,out))return false;
        size_t falseJump=out.size();out.push_back({Op::JumpFalse,{},"",0,e->line});
        if(!compileValueExpr(b->rhs.get(),locals,out))return false;out.push_back({Op::ToBool,{},"",0,e->line});
        size_t done=out.size();out.push_back({Op::Jump,{},"",0,e->line});
        out[falseJump].arg=(int)out.size();out.push_back({Op::Const,Value::Bool(false),"",0,e->line});out[done].arg=(int)out.size();return true;
    }
    if(b->op=="OR"){
        if(!compileValueExpr(b->lhs.get(),locals,out))return false;
        size_t evalRight=out.size();out.push_back({Op::JumpFalse,{},"",0,e->line});
        out.push_back({Op::Const,Value::Bool(true),"",0,e->line});size_t done=out.size();out.push_back({Op::Jump,{},"",0,e->line});
        out[evalRight].arg=(int)out.size();if(!compileValueExpr(b->rhs.get(),locals,out))return false;out.push_back({Op::ToBool,{},"",0,e->line});out[done].arg=(int)out.size();return true;
    }
    if(!compileValueExpr(b->lhs.get(),locals,out)||!compileValueExpr(b->rhs.get(),locals,out))return false;
    Op op;
    if(b->op=="+")op=Op::Add;else if(b->op=="-")op=Op::Sub;else if(b->op=="*")op=Op::Mul;
    else if(b->op=="/")op=Op::Div;else if(b->op=="%")op=Op::Mod;else if(b->op=="==")op=Op::Eq;
    else if(b->op=="!=")op=Op::Ne;else if(b->op=="<")op=Op::Lt;else if(b->op==">")op=Op::Gt;
    else if(b->op=="<=")op=Op::Le;else if(b->op==">=")op=Op::Ge;else return false;
    out.push_back({op,{},"",0,e->line});return true;
}

bool compileValueBlock(const Block& block,const std::unordered_map<std::string,int>& locals,
                       std::vector<FastValueProgram::Instr>& out,int& loopCount,
                       std::vector<size_t>* breakJumps=nullptr,
                       std::vector<size_t>* continueJumps=nullptr,bool topLevel=false){
    using Op=FastValueProgram::Op;
    for(const auto& s:block){
        if(topLevel&&(dynamic_cast<const ForStmt*>(s.get())||dynamic_cast<const WhileStmt*>(s.get()))){
            FastValueProgram::Instr instruction{Op::ExecLoopStmt};
            instruction.line=s->line;instruction.stmt=s.get();out.push_back(std::move(instruction));
            continue;
        }
        if(auto* a=dynamic_cast<const AssignStmt*>(s.get())){
            auto it=locals.find(a->name);
            if(a->isInput){
                if(a->inputPrompt){if(!compileValueExpr(a->inputPrompt.get(),locals,out))return false;}
                else out.push_back({Op::Const,Value::Str(""),"",0,a->line});
                if(topLevel)out.push_back({Op::ReadInputGlobal,{},a->name,0,a->line});
                else{if(it==locals.end())return false;out.push_back({Op::ReadInputLocal,{},"",it->second,a->line});}
            }else if(a->memberBase){
                if(!compileValueExpr(a->memberBase.get(),locals,out)||!compileValueExpr(a->value.get(),locals,out))return false;
                out.push_back({Op::MemberSet,{},a->memberName,0,a->line});
            }else if(a->index){
                if(!compileValueExpr(a->index.get(),locals,out)||!compileValueExpr(a->value.get(),locals,out))return false;
                if(it!=locals.end())out.push_back({Op::IndexSetLocal,{},"",it->second,a->line});
                else out.push_back({Op::IndexSetGlobal,{},a->name,0,a->line});
            }else{
                if(!compileValueExpr(a->value.get(),locals,out))return false;
                if(topLevel)out.push_back({Op::StoreGlobal,{},a->name,0,a->line});
                else{if(it==locals.end())return false;out.push_back({Op::StoreLocal,{},"",it->second,a->line});}
            }
        }else if(auto* r=dynamic_cast<const ReturnStmt*>(s.get())){
            if(r->value){if(!compileValueExpr(r->value.get(),locals,out))return false;}
            else out.push_back({Op::Const,Value::Nil(),"",0,r->line});
            out.push_back({Op::Return,{},"",0,r->line});
        }else if(auto* i=dynamic_cast<const IfStmt*>(s.get())){
            if(!compileValueExpr(i->cond.get(),locals,out))return false;
            size_t jf=out.size();out.push_back({Op::JumpFalse,{},"",0,i->line});
            if(!compileValueBlock(i->thenBlock,locals,out,loopCount,breakJumps,continueJumps,topLevel))return false;
            if(i->elseBlock.empty())out[jf].arg=(int)out.size();
            else{size_t j=out.size();out.push_back({Op::Jump,{},"",0,i->line});out[jf].arg=(int)out.size();
                 if(!compileValueBlock(i->elseBlock,locals,out,loopCount,breakJumps,continueJumps,topLevel))return false;out[j].arg=(int)out.size();}
        }else if(auto* w=dynamic_cast<const WhileStmt*>(s.get())){
            size_t start=out.size();if(!compileValueExpr(w->cond.get(),locals,out))return false;
            size_t done=out.size();out.push_back({Op::JumpFalse,{},"",0,w->line});
            std::vector<size_t> breaks,continues;
            if(!compileValueBlock(w->body,locals,out,loopCount,&breaks,&continues,topLevel))return false;
            for(size_t j:continues)out[j].arg=(int)start;
            out.push_back({Op::Jump,{},"",(int)start,w->line});
            out[done].arg=(int)out.size();for(size_t j:breaks)out[j].arg=(int)out.size();
        }else if(auto* f=dynamic_cast<const ForStmt*>(s.get())){
            auto var=locals.find(f->varName);if((!topLevel&&var==locals.end())||loopCount>=16)return false;
            if(!compileValueExpr(f->fromExpr.get(),locals,out)||!compileValueExpr(f->toExpr.get(),locals,out))return false;
            if(f->stepExpr){if(!compileValueExpr(f->stepExpr.get(),locals,out))return false;}
            else out.push_back({Op::Const,Value::Int(1),"",0,f->line});
            int state=loopCount++;size_t init=out.size();
            if(topLevel)out.push_back({Op::ForInitGlobal,{},f->varName,state,f->line});
            else out.push_back({Op::ForInit,{},"",state,f->line,var->second});
            size_t bodyStart=out.size();std::vector<size_t> breaks,continues;
            if(!compileValueBlock(f->body,locals,out,loopCount,&breaks,&continues,topLevel))return false;
            size_t next=out.size();for(size_t j:continues)out[j].arg=(int)next;
            if(topLevel)out.push_back({Op::ForNextGlobal,{},f->varName,state,f->line});
            else out.push_back({Op::ForNext,{},"",state,f->line,var->second});
            out.back().value=Value::Int((long long)bodyStart);
            out[init].value=Value::Int((long long)out.size());for(size_t j:breaks)out[j].arg=(int)out.size();
        }else if(dynamic_cast<const BreakStmt*>(s.get())){
            if(!breakJumps)return false;breakJumps->push_back(out.size());out.push_back({Op::Jump,{},"",0,s->line});
        }else if(dynamic_cast<const ContinueStmt*>(s.get())){
            if(!continueJumps)return false;continueJumps->push_back(out.size());out.push_back({Op::Jump,{},"",0,s->line});
        }else if(auto* t=dynamic_cast<const TryStmt*>(s.get())){
            int errIndex=-1;
            if(!t->errVarName.empty()){
                if(topLevel)errIndex=-2;
                else{auto it=locals.find(t->errVarName);if(it==locals.end())return false;errIndex=it->second;}
            }
            size_t push=out.size();out.push_back({Op::PushHandler,{},t->errVarName,0,t->line,errIndex});
            if(!compileValueBlock(t->tryBlock,locals,out,loopCount,breakJumps,continueJumps,topLevel))return false;
            out.push_back({Op::PopHandler,{},"",0,t->line});
            size_t done=out.size();out.push_back({Op::Jump,{},"",0,t->line});
            out[push].arg=(int)out.size();
            if(!compileValueBlock(t->catchBlock,locals,out,loopCount,breakJumps,continueJumps,topLevel))return false;
            out[done].arg=(int)out.size();
        }else if(auto* p=dynamic_cast<const PrintStmt*>(s.get())){
            if(p->value){if(!compileValueExpr(p->value.get(),locals,out))return false;}
            else out.push_back({Op::Const,Value::Str(""),"",0,p->line});
            out.push_back({Op::Print,{},"",0,p->line});
        }else if(auto* input=dynamic_cast<const InputPauseStmt*>(s.get())){
            if(input->prompt){if(!compileValueExpr(input->prompt.get(),locals,out))return false;}
            else out.push_back({Op::Const,Value::Str(""),"",0,input->line});
            out.push_back({Op::PauseInput,{},"",0,input->line});
        }else if(auto* x=dynamic_cast<const ExprStmt*>(s.get())){
            if(!compileValueExpr(x->expr.get(),locals,out))return false;
            out.push_back({Op::Pop,{},"",0,x->line});
        }else if(topLevel&&(dynamic_cast<const FuncDefStmt*>(s.get())||dynamic_cast<const ClassDefStmt*>(s.get()))){
        }else return false;
    }return true;
}

std::shared_ptr<FastValueProgram> buildFastValueProgram(const std::vector<std::string>& params,const Block& body,bool includeSelf=false){
    auto p=std::make_shared<FastValueProgram>();p->paramCount=params.size()+(includeSelf?1:0);
    if(p->paramCount>64)return {};
    std::unordered_map<std::string,int> locals;
    if(includeSelf){locals["SELF"]=(int)locals.size();p->localNames.push_back("SELF");p->localTypes.push_back(Type::OBJECT);}
    for(const auto& n:params){locals[n]=(int)locals.size();p->localNames.push_back(n);p->localTypes.push_back(Interpreter::typeOfName(n,0));}
    std::function<bool(const Block&)> collect=[&](const Block& b){for(const auto& s:b){
        if(auto* a=dynamic_cast<const AssignStmt*>(s.get())){
            if(!a->index&&!a->memberBase&&!locals.count(a->name)){if(locals.size()>=64)return false;locals[a->name]=(int)locals.size();p->localNames.push_back(a->name);p->localTypes.push_back(Interpreter::typeOfName(a->name,a->line));}}
        else if(auto* i=dynamic_cast<const IfStmt*>(s.get())){if(!collect(i->thenBlock)||!collect(i->elseBlock))return false;}
        else if(auto* w=dynamic_cast<const WhileStmt*>(s.get())){if(!collect(w->body))return false;}
        else if(auto* f=dynamic_cast<const ForStmt*>(s.get())){
            if(!locals.count(f->varName)){if(locals.size()>=64)return false;locals[f->varName]=(int)locals.size();p->localNames.push_back(f->varName);p->localTypes.push_back(Interpreter::typeOfName(f->varName,f->line));}
            if(!collect(f->body))return false;
        }
        else if(auto* t=dynamic_cast<const TryStmt*>(s.get())){
            if(!t->errVarName.empty()&&!locals.count(t->errVarName)){if(locals.size()>=64)return false;locals[t->errVarName]=(int)locals.size();p->localNames.push_back(t->errVarName);p->localTypes.push_back(Interpreter::typeOfName(t->errVarName,t->line));}
            if(!collect(t->tryBlock)||!collect(t->catchBlock))return false;
        }
    }return true;};
    if(!collect(body))return {};
    int loopCount=0;if(!compileValueBlock(body,locals,p->code,loopCount))return {};
    p->loopCount=(size_t)loopCount;
    return p;
}

std::shared_ptr<FastValueProgram> buildTopLevelValueProgram(const Block& body){
    auto p=std::make_shared<FastValueProgram>();
    std::unordered_map<std::string,int> noLocals;
    int loopCount=0;
    if(!compileValueBlock(body,noLocals,p->code,loopCount,nullptr,nullptr,true))return {};
    p->loopCount=(size_t)loopCount;
    return p;
}

std::shared_ptr<FastValueProgram> buildValueExpressionProgram(const Expr* expression){
    auto p=std::make_shared<FastValueProgram>();
    std::unordered_map<std::string,int> noLocals;
    if(!compileValueExpr(expression,noLocals,p->code))return {};
    p->code.push_back({FastValueProgram::Op::Return,{},"",0,expression->line});
    return p;
}

bool compileFastFuncExpr(const Expr* e, const std::string& self,
                         const std::unordered_map<std::string,int>& locals,
                         std::vector<FastIntProgram::Instr>& out) {
    using Op = FastIntProgram::Op;
    if (auto* v = dynamic_cast<const IntLitExpr*>(e)) {
        out.push_back({Op::Const, v->value}); return true;
    }
    if (auto* v = dynamic_cast<const BoolLitExpr*>(e)) {
        out.push_back({Op::Const, v->value ? 1 : 0}); return true;
    }
    if (auto* v = dynamic_cast<const NameExpr*>(e)) {
        auto it = locals.find(v->name);
        if (it == locals.end()) return false;
        out.push_back({Op::Local, 0, it->second}); return true;
    }
    if (auto* c = dynamic_cast<const CallExpr*>(e)) {
        if (c->args.size() > 8) return false;
        if (c->name != self &&
            (!gKnownFastIntFunctions || !gKnownFastIntFunctions->count(c->name))) return false;
        for (const auto& a : c->args)
            if (!compileFastFuncExpr(a.get(), self, locals, out)) return false;
        if (c->name == self) out.push_back({Op::CallSelf, 0, (int)c->args.size()});
        else out.push_back({Op::CallNamed, 0, (int)c->args.size(), 0, c->name});
        return true;
    }
    auto* b = dynamic_cast<const BinaryExpr*>(e);
    if (!b || !compileFastFuncExpr(b->lhs.get(), self, locals, out) ||
        !compileFastFuncExpr(b->rhs.get(), self, locals, out)) return false;
    Op op;
    if (b->op=="+") op=Op::Add; else if (b->op=="-") op=Op::Sub;
    else if (b->op=="*") op=Op::Mul; else if (b->op=="%") op=Op::Mod;
    else if (b->op=="==") op=Op::Eq; else if (b->op=="!=") op=Op::Ne;
    else if (b->op=="<") op=Op::Lt; else if (b->op==">") op=Op::Gt;
    else if (b->op=="<=") op=Op::Le; else if (b->op==">=") op=Op::Ge;
    else return false;
    out.push_back({op}); return true;
}

bool compileFastFuncBlock(const Block& block, const std::string& self,
                          const std::unordered_map<std::string,int>& locals,
                          std::vector<FastIntProgram::Instr>& out, int& loopCount,
                          std::vector<size_t>* breakJumps = nullptr,
                          std::vector<size_t>* continueJumps = nullptr) {
    using Op = FastIntProgram::Op;
    for (const auto& s : block) {
        if (auto* a = dynamic_cast<const AssignStmt*>(s.get())) {
            auto it = locals.find(a->name);
            if (a->isInput || a->index || a->memberBase || it == locals.end() ||
                !compileFastFuncExpr(a->value.get(), self, locals, out)) return false;
            out.push_back({Op::StoreLocal, 0, it->second});
        } else if (auto* r = dynamic_cast<const ReturnStmt*>(s.get())) {
            if (!r->value || !compileFastFuncExpr(r->value.get(), self, locals, out)) return false;
            out.push_back({Op::Return});
        } else if (auto* i = dynamic_cast<const IfStmt*>(s.get())) {
            if (!compileFastFuncExpr(i->cond.get(), self, locals, out)) return false;
            size_t jf=out.size(); out.push_back({Op::JumpFalse});
            if (!compileFastFuncBlock(i->thenBlock,self,locals,out,loopCount,breakJumps,continueJumps)) return false;
            if (i->elseBlock.empty()) out[jf].value=(long long)out.size();
            else {
                size_t j=out.size(); out.push_back({Op::Jump});
                out[jf].value=(long long)out.size();
                if (!compileFastFuncBlock(i->elseBlock,self,locals,out,loopCount,breakJumps,continueJumps)) return false;
                out[j].value=(long long)out.size();
            }
        } else if (auto* w = dynamic_cast<const WhileStmt*>(s.get())) {
            size_t start = out.size();
            if (!compileFastFuncExpr(w->cond.get(), self, locals, out)) return false;
            size_t done = out.size(); out.push_back({Op::JumpFalse});
            std::vector<size_t> breaks, continues;
            if (!compileFastFuncBlock(w->body, self, locals, out, loopCount, &breaks, &continues)) return false;
            for (size_t j : continues) out[j].value = (long long)start;
            out.push_back({Op::Jump, (long long)start});
            out[done].value = (long long)out.size();
            for (size_t j : breaks) out[j].value = (long long)out.size();
        } else if (auto* f = dynamic_cast<const ForStmt*>(s.get())) {
            auto var = locals.find(f->varName);
            if (var == locals.end() || loopCount >= 16) return false;
            if (!compileFastFuncExpr(f->fromExpr.get(), self, locals, out) ||
                !compileFastFuncExpr(f->toExpr.get(), self, locals, out)) return false;
            if (f->stepExpr) {
                if (!compileFastFuncExpr(f->stepExpr.get(), self, locals, out)) return false;
            } else out.push_back({Op::Const, 1});
            int state = loopCount++;
            size_t init = out.size(); out.push_back({Op::ForInit, 0, state, var->second});
            size_t bodyStart = out.size();
            std::vector<size_t> breaks, continues;
            if (!compileFastFuncBlock(f->body, self, locals, out, loopCount, &breaks, &continues)) return false;
            size_t next = out.size();
            for (size_t j : continues) out[j].value = (long long)next;
            out.push_back({Op::ForNext, (long long)bodyStart, state, var->second});
            out[init].value = (long long)out.size();
            for (size_t j : breaks) out[j].value = (long long)out.size();
        } else if (dynamic_cast<const BreakStmt*>(s.get())) {
            if (!breakJumps) return false;
            breakJumps->push_back(out.size()); out.push_back({Op::Jump});
        } else if (dynamic_cast<const ContinueStmt*>(s.get())) {
            if (!continueJumps) return false;
            continueJumps->push_back(out.size()); out.push_back({Op::Jump});
        } else return false;
    }
    return true;
}

std::shared_ptr<FastIntProgram> buildFastIntProgram(const std::string& name,
                                                    const std::vector<std::string>& params,
                                                    const Block& body,
                                                    const std::unordered_set<std::string>& knownFastFunctions) {
    if (params.size() > 8) return {};
    std::unordered_map<std::string,int> locals;
    for (size_t i=0;i<params.size();++i) {
        if (params[i].rfind("INT_",0)!=0) return {};
        locals[params[i]]=(int)i;
    }
    std::function<bool(const Block&)> collect = [&](const Block& block) {
        for (const auto& s : block) {
            if (auto* a = dynamic_cast<const AssignStmt*>(s.get())) {
                if (a->isInput || a->index || a->memberBase || a->name.rfind("INT_",0)!=0) return false;
                if (!locals.count(a->name)) {
                    if (locals.size() >= 64) return false;
                    locals[a->name] = (int)locals.size();
                }
            } else if (auto* i = dynamic_cast<const IfStmt*>(s.get())) {
                if (!collect(i->thenBlock) || !collect(i->elseBlock)) return false;
            } else if (auto* w = dynamic_cast<const WhileStmt*>(s.get())) {
                if (!collect(w->body)) return false;
            } else if (auto* f = dynamic_cast<const ForStmt*>(s.get())) {
                if (f->varName.rfind("INT_",0)!=0) return false;
                if (!locals.count(f->varName)) {
                    if (locals.size() >= 64) return false;
                    locals[f->varName] = (int)locals.size();
                }
                if (!collect(f->body)) return false;
            }
        }
        return true;
    };
    if (!collect(body)) return {};
    auto p=std::make_shared<FastIntProgram>(); p->paramCount=params.size(); p->localCount=locals.size();
    int loopCount = 0;
    gKnownFastIntFunctions = &knownFastFunctions;
    bool compiled = compileFastFuncBlock(body,name,locals,p->code,loopCount);
    gKnownFastIntFunctions = nullptr;
    if (!compiled) return {};
    p->loopCount = (size_t)loopCount;
    return p;
}

long long runFastIntProgram(Interpreter& interp, const FastIntProgram& p,
                            const long long* args, int depth, int line) {
    if (depth > 256) failAt(line,"превышена максимальная глубина вызовов функций");
    struct LoopState { long long cur=0, end=0, step=1; } loops[16];
    long long locals[64]{}; long long stack[128]{}; size_t sp=0, pc=0;
    for(size_t i=0;i<p.paramCount;++i) locals[i]=args[i];
    auto pop=[&](){return stack[--sp];};
    while(pc<p.code.size()) {
        const auto& in=p.code[pc++];
        interp.consumeInstruction(line);
        using Op=FastIntProgram::Op;
        switch(in.op) {
            case Op::Const: stack[sp++]=in.value; break;
            case Op::Local: stack[sp++]=locals[in.arg]; break;
            case Op::StoreLocal: locals[in.arg]=pop(); break;
            case Op::Add:{auto r=pop(),l=pop();stack[sp++]=l+r;break;}
            case Op::Sub:{auto r=pop(),l=pop();stack[sp++]=l-r;break;}
            case Op::Mul:{auto r=pop(),l=pop();stack[sp++]=l*r;break;}
            case Op::Mod:{auto r=pop(),l=pop();if(!r)failAt(line,"деление на ноль (%)");stack[sp++]=l%r;break;}
            case Op::Eq:{auto r=pop(),l=pop();stack[sp++]=l==r;break;}
            case Op::Ne:{auto r=pop(),l=pop();stack[sp++]=l!=r;break;}
            case Op::Lt:{auto r=pop(),l=pop();stack[sp++]=l<r;break;}
            case Op::Gt:{auto r=pop(),l=pop();stack[sp++]=l>r;break;}
            case Op::Le:{auto r=pop(),l=pop();stack[sp++]=l<=r;break;}
            case Op::Ge:{auto r=pop(),l=pop();stack[sp++]=l>=r;break;}
            case Op::JumpFalse:if(!pop())pc=(size_t)in.value;break;
            case Op::Jump:pc=(size_t)in.value;break;
            case Op::ForInit:{
                auto step=pop(), end=pop(), from=pop();
                if(!step) failAt(line,"шаг цикла FOR (STEP) не может быть равен нулю");
                loops[in.arg]={from,end,step};
                if(step>0 ? from>end : from<end) pc=(size_t)in.value;
                else locals[in.arg2]=from;
                break;
            }
            case Op::ForNext:{
                auto& loop=loops[in.arg]; loop.cur+=loop.step;
                if(loop.step>0 ? loop.cur<=loop.end : loop.cur>=loop.end){
                    locals[in.arg2]=loop.cur; pc=(size_t)in.value;
                }
                break;
            }
            case Op::CallSelf:{
                long long callArgs[8]{};
                for(int i=in.arg-1;i>=0;--i)callArgs[i]=pop();
                stack[sp++]=runFastIntProgram(interp,p,callArgs,depth+1,line);break;
            }
            case Op::CallNamed:{
                std::vector<Value> callArgs((size_t)in.arg);
                for(int i=in.arg-1;i>=0;--i)callArgs[(size_t)i]=Value::Int(pop());
                Value result=interp.callFunction(in.text,callArgs,line);
                if(result.type!=Type::INT) failAt(line,"байткод ожидал INT-результат функции '"+in.text+"'");
                stack[sp++]=result.i;break;
            }
            case Op::Return:return pop();
        }
        if(sp>=128) failAt(line,"переполнение стека байткода функции");
    }
    return 0;
}

Value runFastValueProgram(Interpreter& interp,const FastValueProgram& p,
                          const std::vector<Value>& args,int line){
    using Op=FastValueProgram::Op;
    std::vector<Value> locals(p.localNames.size());
    for(size_t i=0;i<p.paramCount;++i)locals[i]=interp.coerce(p.localTypes[i],args[i],line);
    struct LoopState{double cur=0,end=0,step=1;Type type=Type::INT;} loops[16];
    struct Handler{size_t catchPc;int errIndex;std::string errName;};std::vector<Handler> handlers;
    std::vector<Value> stack;stack.reserve(64);size_t pc=0;
    auto pop=[&](){Value v=std::move(stack.back());stack.pop_back();return v;};
    while(pc<p.code.size()){
        const auto& in=p.code[pc++];
        interp.consumeInstruction(in.line);
        try{switch(in.op){
            case Op::Const:stack.push_back(in.value);break;
            case Op::Local:stack.push_back(locals[(size_t)in.arg]);break;
            case Op::Global:stack.push_back(interp.getVar(in.text,in.line));break;
            case Op::StoreLocal:{Value v=interp.coerce(p.localTypes[(size_t)in.arg],pop(),in.line);
                if(v.type==Type::ARRAY&&v.arr)v=Value::Array(*v.arr);locals[(size_t)in.arg]=std::move(v);break;}
            case Op::StoreGlobal:{Value v=interp.coerceAssign(in.text,pop(),in.line);interp.setVar(in.text,std::move(v),in.line);break;}
            case Op::IndexSetLocal:{auto value=pop(),index=pop();interp.indexSet(locals[(size_t)in.arg],index,std::move(value),in.line);break;}
            case Op::IndexSetGlobal:{auto value=pop(),index=pop();Value base=interp.getVar(in.text,in.line);interp.indexSet(base,index,std::move(value),in.line);break;}
            case Op::Add:{auto r=pop(),l=pop();stack.push_back(interp.opAdd(l,r,in.line));break;}
            case Op::Sub:{auto r=pop(),l=pop();stack.push_back(interp.opSub(l,r,in.line));break;}
            case Op::Mul:{auto r=pop(),l=pop();stack.push_back(interp.opMul(l,r,in.line));break;}
            case Op::Div:{auto r=pop(),l=pop();stack.push_back(interp.opDiv(l,r,in.line));break;}
            case Op::Mod:{auto r=pop(),l=pop();stack.push_back(interp.opMod(l,r,in.line));break;}
            case Op::Eq:{auto r=pop(),l=pop();stack.push_back(interp.opEq(l,r,in.line));break;}
            case Op::Ne:{auto r=pop(),l=pop();stack.push_back(interp.opNe(l,r,in.line));break;}
            case Op::Lt:{auto r=pop(),l=pop();stack.push_back(interp.opLt(l,r,in.line));break;}
            case Op::Gt:{auto r=pop(),l=pop();stack.push_back(interp.opGt(l,r,in.line));break;}
            case Op::Le:{auto r=pop(),l=pop();stack.push_back(interp.opLe(l,r,in.line));break;}
            case Op::Ge:{auto r=pop(),l=pop();stack.push_back(interp.opGe(l,r,in.line));break;}
            case Op::Neg:stack.push_back(interp.opNeg(pop(),in.line));break;
            case Op::Not:stack.push_back(interp.opNot(pop()));break;
            case Op::ToBool:stack.push_back(Value::Bool(pop().truthy()));break;
            case Op::MakeArray:{std::vector<Value> values((size_t)in.arg);for(int i=in.arg-1;i>=0;--i)values[(size_t)i]=pop();stack.push_back(Value::Array(std::move(values)));break;}
            case Op::IndexGet:{auto index=pop(),base=pop();stack.push_back(interp.indexGet(base,index,in.line));break;}
            case Op::MemberGet:{auto object=pop();stack.push_back(interp.getMemberField(object,in.text,in.line));break;}
            case Op::MemberSet:{auto value=pop(),object=pop();interp.setMemberField(object,in.text,std::move(value),in.line);break;}
            case Op::Call:{std::vector<Value> callArgs((size_t)in.arg);for(int i=in.arg-1;i>=0;--i)callArgs[(size_t)i]=pop();stack.push_back(interp.callFunction(in.text,callArgs,in.line));break;}
            case Op::CallMethod:{std::vector<Value> callArgs((size_t)in.arg);for(int i=in.arg-1;i>=0;--i)callArgs[(size_t)i]=pop();Value object=pop();stack.push_back(interp.callMethodDynamic(object,in.text,callArgs,in.line));break;}
            case Op::Print:std::cout<<pop().toString()<<"\n";break;
            case Op::PauseInput:{std::string prompt=pop().toString();if(!prompt.empty())std::cout<<prompt<<std::flush;std::string dummy;std::getline(std::cin,dummy);break;}
            case Op::ReadInputLocal:{std::string prompt=pop().toString();locals[(size_t)in.arg]=interp.readInputTyped(p.localTypes[(size_t)in.arg],prompt,in.line);break;}
            case Op::ReadInputGlobal:{std::string prompt=pop().toString();Type type=Interpreter::typeOfName(in.text,in.line);interp.setVar(in.text,interp.readInputTyped(type,prompt,in.line),in.line);break;}
            case Op::Pop:pop();break;
            case Op::JumpFalse:if(!pop().truthy())pc=(size_t)in.arg;break;
            case Op::Jump:pc=(size_t)in.arg;break;
            case Op::ForInit:{
                Value stepV=pop(),endV=pop(),fromV=pop();Type type=p.localTypes[(size_t)in.arg2];
                fromV=interp.coerce(type,fromV,in.line);endV=interp.coerce(type,endV,in.line);stepV=interp.coerce(type,stepV,in.line);
                double step=stepV.num(),from=fromV.num(),end=endV.num();if(step==0)failAt(in.line,"шаг цикла FOR (STEP) не может быть равен нулю");
                loops[in.arg]={from,end,step,type};
                if(step>0?from>end:from<end)pc=(size_t)in.value.i;
                else locals[(size_t)in.arg2]=type==Type::INT?Value::Int((long long)from):Value::Float(from);
                break;
            }
            case Op::ForNext:{auto& loop=loops[in.arg];loop.cur+=loop.step;
                if(loop.step>0?loop.cur<=loop.end:loop.cur>=loop.end){locals[(size_t)in.arg2]=loop.type==Type::INT?Value::Int((long long)loop.cur):Value::Float(loop.cur);pc=(size_t)in.value.i;}break;}
            case Op::ForInitGlobal:{Value stepV=pop(),endV=pop(),fromV=pop();Type type=Interpreter::typeOfName(in.text,in.line);
                fromV=interp.coerce(type,fromV,in.line);endV=interp.coerce(type,endV,in.line);stepV=interp.coerce(type,stepV,in.line);
                double step=stepV.num(),from=fromV.num(),end=endV.num();if(step==0)failAt(in.line,"шаг цикла FOR (STEP) не может быть равен нулю");loops[in.arg]={from,end,step,type};
                if(step>0?from>end:from<end)pc=(size_t)in.value.i;else interp.setVar(in.text,type==Type::INT?Value::Int((long long)from):Value::Float(from),in.line);break;}
            case Op::ForNextGlobal:{auto& loop=loops[in.arg];loop.cur+=loop.step;if(loop.step>0?loop.cur<=loop.end:loop.cur>=loop.end){interp.setVar(in.text,loop.type==Type::INT?Value::Int((long long)loop.cur):Value::Float(loop.cur),in.line);pc=(size_t)in.value.i;}break;}
            case Op::PushHandler:handlers.push_back({(size_t)in.arg,in.arg2,in.text});break;
            case Op::PopHandler:if(!handlers.empty())handlers.pop_back();break;
            case Op::ExecLoopStmt:{Signal signal=in.stmt->exec(interp);if(signal==Signal::Return)return interp.takePendingReturn();break;}
            case Op::Return:return pop();
        }}catch(const UmbrlyError& error){
            if(handlers.empty())throw;
            Handler handler=handlers.back();handlers.pop_back();stack.clear();
            if(handler.errIndex>=0)locals[(size_t)handler.errIndex]=interp.coerce(p.localTypes[(size_t)handler.errIndex],Value::Str(error.what()),in.line);
            else if(handler.errIndex==-2&&!handler.errName.empty())interp.setVar(handler.errName,interp.coerceAssign(handler.errName,Value::Str(error.what()),in.line),in.line);
            pc=handler.catchPc;
        }
    }
    return Value::Nil();
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

}  // namespace

Interpreter::Interpreter() {
    globals_.reserve(64);
}

void Interpreter::setExecutionLimits(uint64_t maxInstructions, uint64_t timeoutMs) {
    maxInstructions_ = maxInstructions;
    timeoutMs_ = timeoutMs;
}

void Interpreter::consumeInstruction(int line) {
    if (maxInstructions_ == 0 && timeoutMs_ == 0) return;
    ++instructionCount_;
    if (maxInstructions_ && instructionCount_ > maxInstructions_)
        failAt(line, "превышен лимит инструкций (" + std::to_string(maxInstructions_) + ")");
    if (timeoutMs_ && (instructionCount_ & 1023ULL) == 0 && std::chrono::steady_clock::now() >= deadline_)
        failAt(line, "превышен лимит времени выполнения (" + std::to_string(timeoutMs_) + " мс)");
}

void Interpreter::pushLocalScope() {
    if ((size_t)localDepth_ == localPool_.size()) localPool_.emplace_back();
    localDepth_++;
}

void Interpreter::popLocalScope() {
    localPool_[(size_t)--localDepth_].clear();
}

void Interpreter::registerBuiltin(const std::string& name, NativeFn fn) {
    builtins_[name] = std::move(fn);
}

void Interpreter::registerFunction(const std::string& name, std::vector<std::string> params,
                                    std::shared_ptr<Block> body) {
    std::vector<Type> types;
    types.reserve(params.size());
    for (const auto& p : params) types.push_back(typeOfName(p, 0));
    std::unordered_set<std::string> knownFastFunctions;
    for (const auto& entry : functions_)
        if (entry.second.fastInt) knownFastFunctions.insert(entry.first);
    auto fastInt = buildFastIntProgram(name, params, *body, knownFastFunctions);
    auto fastValue = buildFastValueProgram(params, *body);
    functions_[name] = FuncInfo{std::move(params), std::move(types), std::move(body), std::move(fastInt), std::move(fastValue)};
}

void Interpreter::registerAllDefs(const Block& program) {
    for (const auto& stmt : program) {
        if (auto* fd = dynamic_cast<FuncDefStmt*>(stmt.get())) fd->exec(*this);
        else if (auto* cd = dynamic_cast<ClassDefStmt*>(stmt.get())) cd->exec(*this);
    }
}

void Interpreter::run(const Block& program) {
    instructionCount_ = 0;
    if (timeoutMs_) deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs_);
    // Предпроход: регистрируем все FUNC и CLASS заранее, чтобы работали вызовы "вперёд".
    registerAllDefs(program);
    if (!traceHook_) {
        auto topLevel = buildTopLevelValueProgram(program);
        if (topLevel) {
            std::vector<Value> noArgs;
            runFastValueProgram(*this, *topLevel, noArgs, 0);
            return;
        }
    }
    execBlock(program);
}

Signal Interpreter::execBlock(const Block& block) {
    for (const auto& stmt : block) {
        consumeInstruction(stmt->line);
        if (traceHook_) traceHook_(stmt->line);
        Signal s = stmt->exec(*this);
        if (s != Signal::None) return s;
    }
    return Signal::None;
}

Value Interpreter::getVar(const std::string& name, int line) const {
    if (localDepth_ > 0) {
        const LocalFrame& top = localPool_[(size_t)localDepth_ - 1];
        for (const auto& kv : top)
            if (kv.first == name) return kv.second;
    }
    auto git = globals_.find(name);
    if (git != globals_.end()) return git->second;
    failAt(line, "неизвестная переменная '" + name + "' (переменной ещё не присвоено значение)");
}

Value Interpreter::getVarCached(const std::string& name, int line, void** globalCache) const {
    if (localDepth_ == 0 && globalCache && *globalCache)
        return *static_cast<const Value*>(*globalCache);
    if (localDepth_ == 0) {
        auto it = globals_.find(name);
        if (it == globals_.end())
            failAt(line, "неизвестная переменная '" + name + "'");
        if (globalCache) *globalCache = const_cast<Value*>(&it->second);
        return it->second;
    }
    return getVar(name, line);
}

Value Interpreter::getVarFast(const std::string& name, int line, void** globalCache, int* localIndex) const {
    if (localDepth_ == 0) return getVarCached(name, line, globalCache);
    const LocalFrame& frame = localPool_[(size_t)localDepth_ - 1];
    if (localIndex && *localIndex >= 0 && (size_t)*localIndex < frame.size() &&
        frame[(size_t)*localIndex].first == name)
        return frame[(size_t)*localIndex].second;
    for (size_t i = 0; i < frame.size(); ++i) {
        if (frame[i].first == name) {
            if (localIndex) *localIndex = (int)i;
            return frame[i].second;
        }
    }
    auto it = globals_.find(name);
    if (it != globals_.end()) return it->second;
    failAt(line, "неизвестная переменная '" + name + "'");
}

void Interpreter::setVar(const std::string& name, Value v, int line) {
    (void)line;
    if (localDepth_ == 0) { globals_[name] = std::move(v); return; }
    LocalFrame& top = localPool_[(size_t)localDepth_ - 1];
    for (auto& kv : top) {
        if (kv.first == name) { kv.second = std::move(v); return; }
    }
    top.emplace_back(name, std::move(v));
}

void Interpreter::setVarCached(const std::string& name, Value v, int line, void** globalCache) {
    if (localDepth_ == 0) {
        if (globalCache && *globalCache) {
            *static_cast<Value*>(*globalCache) = std::move(v);
            return;
        }
        auto result = globals_.insert_or_assign(name, std::move(v));
        if (globalCache) *globalCache = &result.first->second;
        return;
    }
    setVar(name, std::move(v), line);
}

void Interpreter::setVarFast(const std::string& name, Value v, int line, void** globalCache, int* localIndex) {
    if (localDepth_ == 0) { setVarCached(name, std::move(v), line, globalCache); return; }
    LocalFrame& frame = localPool_[(size_t)localDepth_ - 1];
    if (localIndex && *localIndex >= 0 && (size_t)*localIndex < frame.size() &&
        frame[(size_t)*localIndex].first == name) {
        frame[(size_t)*localIndex].second = std::move(v);
        return;
    }
    for (size_t i = 0; i < frame.size(); ++i) {
        if (frame[i].first == name) {
            if (localIndex) *localIndex = (int)i;
            frame[i].second = std::move(v);
            return;
        }
    }
    frame.emplace_back(name, std::move(v));
    if (localIndex) *localIndex = (int)frame.size() - 1;
}

void Interpreter::setIndexed(const std::string& name, const Value& index, Value v, int line) {
    Value target = getVar(name, line);
    if (target.type != Type::ARRAY)
        failAt(line, "переменная '" + name + "' не является массивом (ARR_)");
    if (!index.isNum())
        failAt(line, "индекс массива должен быть числом");
    long long i = (long long)index.num();
    if (i < 0 || (size_t)i >= target.arr->size())
        failAt(line, "индекс " + std::to_string(i) + " вне границ массива '" + name +
                          "' (размер " + std::to_string(target.arr->size()) + ")");
    (*target.arr)[(size_t)i] = std::move(v);
}

Value Interpreter::callFunction(const std::string& name, std::vector<Value>& args, int line, void** cache) {
    const FuncInfo* cached = cache ? static_cast<const FuncInfo*>(*cache) : nullptr;
    if (!cached) {
        auto uit = functions_.find(name);
        if (uit != functions_.end()) {
            cached = &uit->second;
            if (cache) *cache = const_cast<FuncInfo*>(cached);
        }
    }
    if (cached) {
        const FuncInfo& fi = *cached;
        if (args.size() != fi.params.size())
            failAt(line, "функция '" + name + "' ожидает " + std::to_string(fi.params.size()) +
                             " аргумент(ов), получено " + std::to_string(args.size()));

        if (!traceHook_ && fi.fastInt) {
            long long raw[8]{};
            bool compatible = true;
            for (size_t k = 0; k < args.size(); ++k) {
                if (args[k].type != Type::INT) { compatible = false; break; }
                raw[k] = args[k].i;
            }
            if (compatible) {
                if (++callDepth_ > kMaxCallDepth) {
                    callDepth_--;
                    failAt(line, "превышена максимальная глубина вызовов функций");
                }
                try {
                    Value result = Value::Int(runFastIntProgram(*this, *fi.fastInt, raw, 1, line));
                    callDepth_--;
                    return result;
                } catch (...) {
                    callDepth_--;
                    throw;
                }
            }
        }

        if (!traceHook_ && fi.fastValue) {
            if (++callDepth_ > kMaxCallDepth) {
                callDepth_--;
                failAt(line, "превышена максимальная глубина вызовов функций");
            }
            try {
                Value result = runFastValueProgram(*this, *fi.fastValue, args, line);
                callDepth_--;
                return result;
            } catch (...) {
                callDepth_--;
                throw;
            }
        }

        if (++callDepth_ > kMaxCallDepth) {
            callDepth_--;
            failAt(line, "превышена максимальная глубина вызовов функций (похоже на бесконечную рекурсию)");
        }

        pushLocalScope();
        LocalFrame& frame = localPool_[(size_t)localDepth_ - 1];
        frame.reserve(fi.params.size());
        for (size_t k = 0; k < fi.params.size(); ++k)
            frame.emplace_back(fi.params[k], coerce(fi.paramTypes[k], args[k], line));

        Signal s;
        try {
            s = execBlock(*fi.body);
        } catch (...) {
            popLocalScope();
            callDepth_--;
            throw;
        }
        Value result = (s == Signal::Return) ? takePendingReturn() : Value::Nil();
        popLocalScope();
        callDepth_--;
        return result;
    }

    auto cit = classes_.find(name);
    if (cit != classes_.end()) return instantiate(name, args, line);

    auto bit = builtins_.find(name);
    if (bit != builtins_.end()) return bit->second(*this, args, line);

    failAt(line, "неизвестная функция '" + name + "'");
}

void Interpreter::registerClass(const ClassDefStmt* def) {
    classes_[def->name] = def;
}

Value Interpreter::instantiate(const std::string& className, std::vector<Value>& args, int line) {
    auto cit = classes_.find(className);
    if (cit == classes_.end()) failAt(line, "неизвестный класс '" + className + "'");
    const ClassDefStmt* def = cit->second;

    Value obj = Value::Object(className);
    for (const auto& f : def->fields) {
        Type ft = typeOfName(f.name, line);
        Value defaultValue;
        if (!traceHook_) {
            auto it = fieldDefaultPrograms_.find(f.defaultValue.get());
            if (it == fieldDefaultPrograms_.end())
                it = fieldDefaultPrograms_.emplace(f.defaultValue.get(), buildValueExpressionProgram(f.defaultValue.get())).first;
            if (it->second) {
                std::vector<Value> noArgs;
                defaultValue = runFastValueProgram(*this, *it->second, noArgs, line);
            } else defaultValue = f.defaultValue->eval(*this);
        } else defaultValue = f.defaultValue->eval(*this);
        obj.obj->fields[f.name] = coerce(ft, defaultValue, line);
    }

    const FuncDefStmt* init = nullptr;
    for (const auto& m : def->methods) {
        if (m->name == "INIT") { init = m.get(); break; }
    }
    if (init) {
        callMethodOn(def, "INIT", obj, args, line);
    } else if (!args.empty()) {
        failAt(line, "класс '" + className + "' не принимает аргументы конструктора (нет метода INIT)");
    }
    return obj;
}

Value Interpreter::callMethod(const std::string& className, const std::string& methodName,
                               Value selfObj, std::vector<Value>& args, int line) {
    auto cit = classes_.find(className);
    if (cit == classes_.end()) failAt(line, "неизвестный класс '" + className + "'");
    return callMethodOn(cit->second, methodName, std::move(selfObj), args, line);
}

Value Interpreter::callMethodDynamic(const Value& obj, const std::string& methodName,
                                      std::vector<Value>& args, int line) {
    if (obj.type != Type::OBJECT) failAt(line, "значение не является объектом — вызов метода невозможен");
    return callMethod(obj.obj->className, methodName, obj, args, line);
}

Value Interpreter::callMethodOn(const ClassDefStmt* def, const std::string& methodName,
                                 Value selfObj, std::vector<Value>& args, int line) {
    const FuncDefStmt* method = nullptr;
    for (const auto& m : def->methods) {
        if (m->name == methodName) { method = m.get(); break; }
    }
    if (!method) failAt(line, "у класса '" + def->name + "' нет метода '" + methodName + "'");

    if (args.size() != method->params.size())
        failAt(line, "метод '" + methodName + "' класса '" + def->name + "' ожидает " +
                         std::to_string(method->params.size()) + " аргумент(ов), получено " +
                         std::to_string(args.size()));

    if (++callDepth_ > kMaxCallDepth) {
        callDepth_--;
        failAt(line, "превышена максимальная глубина вызовов функций (похоже на бесконечную рекурсию)");
    }

    auto vmIt = methodPrograms_.find(method);
    if (vmIt == methodPrograms_.end())
        vmIt = methodPrograms_.emplace(method, buildFastValueProgram(method->params, *method->body, true)).first;
    if (!traceHook_ && vmIt->second) {
        std::vector<Value> vmArgs;
        vmArgs.reserve(args.size() + 1);
        vmArgs.push_back(std::move(selfObj));
        for (const auto& arg : args) vmArgs.push_back(arg);
        try {
            Value result = runFastValueProgram(*this, *vmIt->second, vmArgs, line);
            callDepth_--;
            return result;
        } catch (...) {
            callDepth_--;
            throw;
        }
    }

    pushLocalScope();
    LocalFrame& frame = localPool_[(size_t)localDepth_ - 1];
    frame.reserve(method->params.size() + 1);
    frame.emplace_back("SELF", std::move(selfObj));
    for (size_t k = 0; k < method->params.size(); ++k) {
        Type pt = typeOfName(method->params[k], line);
        frame.emplace_back(method->params[k], coerce(pt, args[k], line));
    }

    Signal s;
    try {
        s = execBlock(*method->body);
    } catch (...) {
        popLocalScope();
        callDepth_--;
        throw;
    }
    Value result = (s == Signal::Return) ? takePendingReturn() : Value::Nil();
    popLocalScope();
    callDepth_--;
    return result;
}

Type Interpreter::typeOfName(const std::string& name, int line) {
    if (name.rfind("INT_", 0) == 0)   return Type::INT;
    if (name.rfind("FLOAT_", 0) == 0) return Type::FLOAT;
    if (name.rfind("STR_", 0) == 0)   return Type::STR;
    if (name.rfind("BOOL_", 0) == 0)  return Type::BOOL;
    if (name.rfind("ARR_", 0) == 0)   return Type::ARRAY;
    if (name.rfind("OBJ_", 0) == 0)   return Type::OBJECT;
    failAt(line, "имя '" + name + "' должно начинаться с INT_, FLOAT_, STR_, BOOL_, ARR_ или OBJ_");
}

Value Interpreter::coerce(Type target, const Value& v, int line) const {
    switch (target) {
        case Type::INT:
            if (v.type == Type::INT || v.type == Type::BOOL) return Value::Int(v.i);
            if (v.type == Type::FLOAT) return Value::Int((long long)v.f);
            failAt(line, "нельзя записать значение типа " + std::string(typeName(v.type)) + " (\"" +
                             v.toString() + "\") в INT-переменную");
        case Type::FLOAT:
            if (v.isNum()) return Value::Float(v.num());
            failAt(line, "нельзя записать значение типа " + std::string(typeName(v.type)) + " (\"" +
                             v.toString() + "\") в FLOAT-переменную");
        case Type::BOOL:
            return Value::Bool(v.truthy());
        case Type::ARRAY:
            if (v.type == Type::ARRAY) return v;
            failAt(line, "нельзя записать значение типа " + std::string(typeName(v.type)) +
                             " в ARR-переменную");
        case Type::OBJECT:
            if (v.type == Type::OBJECT) return v;  // ссылка на тот же экземпляр, без копирования
            failAt(line, "нельзя записать значение типа " + std::string(typeName(v.type)) +
                             " в OBJ-переменную");
        default:
            return Value::Str(v.toString());
    }
}

Value Interpreter::readInputTyped(Type t, const std::string& prompt, int line) {
    std::cout << prompt << std::flush;
    std::string raw;
    if (!std::getline(std::cin, raw)) raw.clear();
    if (!raw.empty() && raw.back() == '\r') raw.pop_back();
    std::string s = trim(raw);

    if (t == Type::STR) return Value::Str(s);

    if (t == Type::BOOL) {
        std::string up = s;
        for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
        if (up == "TRUE" || up == "1" || up == "YES" || up == "ДА")  return Value::Bool(true);
        if (up == "FALSE" || up == "0" || up == "NO" || up == "НЕТ") return Value::Bool(false);
        failAt(line, "ожидалось TRUE/FALSE, а введено: '" + s + "'");
    }

    if (t == Type::ARRAY) failAt(line, "ввод в переменную-массив (ARR_) не поддерживается");

    try {
        size_t idx = 0;
        if (t == Type::INT) {
            long long v = std::stoll(s, &idx);
            if (idx != s.size()) throw std::invalid_argument("trailing");
            return Value::Int(v);
        }
        double v = std::stod(s, &idx);
        if (idx != s.size()) throw std::invalid_argument("trailing");
        return Value::Float(v);
    } catch (const UmbrlyError&) {
        throw;
    } catch (...) {
        failAt(line, "ожидалось число, а введено: '" + s + "'");
    }
}

Value Interpreter::coerceAssign(const std::string& name, const Value& v, int line) const {
    Type ty = typeOfName(name, line);
    Value c = coerce(ty, v, line);
    // Присваивание массива копирует, а не создаёт alias — см. AssignStmt::exec/README.
    if (ty == Type::ARRAY && c.arr) c = Value::Array(*c.arr);
    return c;
}

namespace {

bool intLike(const Value& v) { return v.type == Type::INT || v.type == Type::BOOL; }
bool bothIntLike(const Value& l, const Value& r) { return intLike(l) && intLike(r); }

void requireNums(const Value& l, const Value& r, const char* op, int line) {
    if (!l.isNum() || !r.isNum())
        failAt(line, std::string("оператор ") + op + " работает только с числами");
}

int compareValues(const Value& l, const Value& r, int line) {
    if (l.type == Type::STR || r.type == Type::STR) {
        if (l.type != Type::STR || r.type != Type::STR)
            failAt(line, "нельзя сравнивать строку со значением другого типа");
        int c = l.s.compare(r.s);
        return (c > 0) - (c < 0);
    }
    if (l.type == Type::ARRAY || r.type == Type::ARRAY)
        failAt(line, "массивы (ARR_) нельзя сравнивать операторами сравнения");
    if (l.type == Type::OBJECT || r.type == Type::OBJECT)
        failAt(line, "объекты (OBJ_) нельзя сравнивать операторами сравнения");
    double a = l.num(), b = r.num();
    return (a > b) - (a < b);
}

}  // namespace

Value Interpreter::opAdd(const Value& l, const Value& r, int line) const {
    if (l.type == Type::STR || r.type == Type::STR) return Value::Str(l.toString() + r.toString());
    if (l.type == Type::ARRAY || r.type == Type::ARRAY) failAt(line, "оператор + не поддерживает массивы");
    requireNums(l, r, "+", line);
    return bothIntLike(l, r) ? Value::Int(l.i + r.i) : Value::Float(l.num() + r.num());
}

Value Interpreter::opSub(const Value& l, const Value& r, int line) const {
    requireNums(l, r, "-", line);
    return bothIntLike(l, r) ? Value::Int(l.i - r.i) : Value::Float(l.num() - r.num());
}

Value Interpreter::opMul(const Value& l, const Value& r, int line) const {
    requireNums(l, r, "*", line);
    return bothIntLike(l, r) ? Value::Int(l.i * r.i) : Value::Float(l.num() * r.num());
}

Value Interpreter::opDiv(const Value& l, const Value& r, int line) const {
    requireNums(l, r, "/", line);
    if (r.num() == 0.0) failAt(line, "деление на ноль");
    if (bothIntLike(l, r) && r.i != 0 && l.i % r.i == 0) return Value::Int(l.i / r.i);
    return Value::Float(l.num() / r.num());
}

Value Interpreter::opMod(const Value& l, const Value& r, int line) const {
    if (!bothIntLike(l, r)) failAt(line, "оператор % работает только с целыми числами");
    if (r.i == 0) failAt(line, "деление на ноль (%)");
    return Value::Int(l.i % r.i);
}

Value Interpreter::opEq(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) == 0); }
Value Interpreter::opNe(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) != 0); }
Value Interpreter::opLt(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) < 0); }
Value Interpreter::opGt(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) > 0); }
Value Interpreter::opLe(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) <= 0); }
Value Interpreter::opGe(const Value& l, const Value& r, int line) const { return Value::Bool(compareValues(l, r, line) >= 0); }

Value Interpreter::opNeg(const Value& v, int line) const {
    if (!v.isNum()) failAt(line, "унарный минус применим только к числам");
    return v.type == Type::FLOAT ? Value::Float(-v.f) : Value::Int(-v.i);
}

Value Interpreter::opNot(const Value& v) const { return Value::Bool(!v.truthy()); }

Value Interpreter::indexGet(const Value& base, const Value& idx, int line) const {
    if (base.type != Type::ARRAY) failAt(line, "значение не является массивом — индексация [ ] невозможна");
    if (!idx.isNum()) failAt(line, "индекс массива должен быть числом");
    long long i = (long long)idx.num();
    if (i < 0 || (size_t)i >= base.arr->size())
        failAt(line, "индекс " + std::to_string(i) + " вне границ массива (размер " +
                          std::to_string(base.arr->size()) + ")");
    return (*base.arr)[(size_t)i];
}

void Interpreter::indexSet(const Value& base, const Value& idx, Value v, int line) const {
    if (base.type != Type::ARRAY) failAt(line, "значение не является массивом — индексация [ ] невозможна");
    if (!idx.isNum()) failAt(line, "индекс массива должен быть числом");
    long long i = (long long)idx.num();
    if (i < 0 || (size_t)i >= base.arr->size())
        failAt(line, "индекс " + std::to_string(i) + " вне границ массива (размер " +
                          std::to_string(base.arr->size()) + ")");
    (*base.arr)[(size_t)i] = std::move(v);
}

Value Interpreter::getMemberField(const Value& obj, const std::string& fieldName, int line) const {
    if (obj.type != Type::OBJECT) failAt(line, "значение не является объектом — обращение через '.' невозможно");
    auto it = obj.obj->fields.find(fieldName);
    if (it == obj.obj->fields.end())
        failAt(line, "у объекта класса '" + obj.obj->className + "' нет поля '" + fieldName + "'");
    return it->second;
}

void Interpreter::setMemberField(const Value& obj, const std::string& fieldName, Value v, int line) const {
    if (obj.type != Type::OBJECT) failAt(line, "слева от '.' должен быть объект");
    Type ft = typeOfName(fieldName, line);
    Value c = coerce(ft, v, line);
    if (ft == Type::ARRAY && c.arr) c = Value::Array(*c.arr);
    obj.obj->fields[fieldName] = std::move(c);
}

}  // namespace umbrly
