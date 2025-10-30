// Single-file mini compiler with: Lexer, hash-based Symbol Table,
// Recursive-descent Parser, TAC (Three-Address Code) generation,
// Pseudo Assembly generation (--asm),
// Grammar tools: FIRST/FOLLOW, Left Recursion Elimination, Left Factoring (--demo-grammar)
//
// Build: g++ -std=c++17 main.cpp -O2 -o my_compiler
// TAC:   ./my_compiler < program.src
// ASM:   ./my_compiler --asm < program.src
// GR:    ./my_compiler --demo-grammar

#include <bits/stdc++.h>
using namespace std;

/*==============================*
 * 1) TOKENS & MANUAL LEXER     *
 *==============================*/
enum class Tok {
    End, Id, Num,
    KwInt, KwIf, KwElse, KwWhile, KwPrint,
    Plus, Minus, Mul, Div,
    Assign, LParen, RParen, LBrace, RBrace, Semicolon
};
struct Token { Tok t; string lex; int line, col; };

struct Lexer {
    string s; int i=0, n=0, line=1, col=1;
    unordered_set<string> kw = {"int","if","else","while","print"};
    Lexer(const string& src): s(src), n((int)src.size()) {}
    char peek() const { return i<n? s[i]: '\0'; }
    char get() { char c=peek(); if(!c) return c; i++; if(c=='\n'){line++; col=1;} else col++; return c; }
    static bool isid0(char c){ return isalpha((unsigned char)c) || c=='_'; }
    static bool isidn(char c){ return isalnum((unsigned char)c) || c=='_'; }

    Token next() {
        while(isspace((unsigned char)peek())) get();
        int L=line, C=col; char c=peek();
        if(!c) return {Tok::End,"",L,C};

        if(isdigit((unsigned char)c)){
            string num; while(isdigit((unsigned char)peek())) num+=get();
            return {Tok::Num,num,L,C};
        }
        if(isid0(c)){
            string id; id+=get();
            while(isidn(peek())) id+=get();
            if(kw.count(id)){
                if(id=="int")   return {Tok::KwInt,id,L,C};
                if(id=="if")    return {Tok::KwIf,id,L,C};
                if(id=="else")  return {Tok::KwElse,id,L,C};
                if(id=="while") return {Tok::KwWhile,id,L,C};
                if(id=="print") return {Tok::KwPrint,id,L,C};
            }
            return {Tok::Id,id,L,C};
        }
        get();
        switch(c){
            case '+': return {Tok::Plus,"+",L,C};
            case '-': return {Tok::Minus,"-",L,C};
            case '*': return {Tok::Mul,"*",L,C};
            case '/': return {Tok::Div,"/",L,C};
            case '=': return {Tok::Assign,"=",L,C};
            case '(': return {Tok::LParen,"(",L,C};
            case ')': return {Tok::RParen,")",L,C};
            case '{': return {Tok::LBrace,"{",L,C};
            case '}': return {Tok::RBrace,"}",L,C};
            case ';': return {Tok::Semicolon,";",L,C};
        }
        throw runtime_error("Unknown character at "+to_string(L)+":"+to_string(C));
    }
};

/*===============================================*
 * 2) HASH-BASED SYMBOL TABLE (SEPARATE CHAINING) *
 *===============================================*/
struct Sym { string name; string type; int value=0; bool initialized=false; };
struct SymbolTable {
    vector<vector<Sym>> buckets;
    explicit SymbolTable(size_t sz=257): buckets(sz) {}
    size_t idx(const string& k) const { return std::hash<string>{}(k) % buckets.size(); }
    bool declare(const string& name, const string& type){
        auto &b = buckets[idx(name)];
        for(auto &e: b) if(e.name==name) return false;
        b.push_back({name,type,0,false}); return true;
    }
    Sym* find(const string& name){
        auto &b = buckets[idx(name)];
        for(auto &e: b) if(e.name==name) return &e;
        return nullptr;
    }
};

/*=============================*
 * 3) AST NODES & TAC EMITTER  *
 *=============================*/
struct Instr{ string op,a1,a2,res; };
struct TAC {
    vector<Instr> code; int tempCounter=0, labelCounter=0;
    string newTemp(){ return "t"+to_string(++tempCounter); }
    string newLabel(const string& base="L"){ return base+to_string(++labelCounter); }
    void emit(const string& op,const string& a1="",const string& a2="",const string& res=""){ code.push_back({op,a1,a2,res}); }
    void dump(ostream& os=cout) const {
        for(auto &i: code){
            if(i.op=="label") os<<i.res<<":\n";
            else if(i.op=="goto") os<<"    goto "<<i.res<<"\n";
            else if(i.op=="ifz") os<<"    ifz "<<i.a1<<" goto "<<i.res<<"\n";
            else if(i.op=="=")  os<<"    "<<i.res<<" = "<<i.a1<<"\n";
            else if(i.op=="print") os<<"    print "<<i.a1<<"\n";
            else if(!i.a2.empty()) os<<"    "<<i.res<<" = "<<i.a1<<" "<<i.op<<" "<<i.a2<<"\n";
        }
    }
};

struct Node{ virtual ~Node(){} virtual string gen(TAC&, SymbolTable&)=0; };
struct Expr: Node{};
struct Num: Expr{
    int v; explicit Num(int v):v(v){}
    string gen(TAC& t, SymbolTable&) override{
        string tmp=t.newTemp(); t.emit("=",to_string(v),"",tmp); return tmp;
    }
};
struct Var: Expr{
    string name; explicit Var(string n):name(move(n)){}
    string gen(TAC& t, SymbolTable&) override{
        string tmp=t.newTemp(); t.emit("=",name,"",tmp); return tmp;
    }
};
struct BinOp: Expr{
    string op; unique_ptr<Expr> a,b;
    BinOp(string op, unique_ptr<Expr> a, unique_ptr<Expr> b)
        :op(move(op)),a(move(a)),b(move(b)){}
    string gen(TAC& t, SymbolTable& s) override{
        string x=a->gen(t,s), y=b->gen(t,s), z=t.newTemp(); t.emit(op,x,y,z); return z;
    }
};
struct Stmt: Node{};
struct Decl: Stmt{
    string name; unique_ptr<Expr> init;
    Decl(string n, unique_ptr<Expr> e):name(move(n)),init(move(e)){}
    string gen(TAC& t, SymbolTable& s) override{
        if(!s.declare(name,"int")) throw runtime_error("Redeclaration: "+name);
        if(init){ string v=init->gen(t,s); t.emit("=",v,"",name); auto *p=s.find(name); if(p){p->initialized=true;} }
        return "";
    }
};
struct Assign: Stmt{
    string name; unique_ptr<Expr> rhs;
    Assign(string n, unique_ptr<Expr> e):name(move(n)),rhs(move(e)){}
    string gen(TAC& t, SymbolTable& s) override{
        if(!s.find(name)) throw runtime_error("Undeclared: "+name);
        string v=rhs->gen(t,s); t.emit("=",v,"",name); s.find(name)->initialized=true; return "";
    }
};
struct Print: Stmt{
    unique_ptr<Expr> e; explicit Print(unique_ptr<Expr> e):e(move(e)){}
    string gen(TAC& t, SymbolTable& s) override{
        string v=e->gen(t,s); t.emit("print",v); return "";
    }
};
struct Block: Stmt{
    vector<unique_ptr<Stmt>> ss;
    string gen(TAC& t, SymbolTable& s) override{ for(auto &st: ss) st->gen(t,s); return ""; }
};
struct IfStmt: Stmt{
    unique_ptr<Expr> cond; unique_ptr<Stmt> thenS; unique_ptr<Stmt> elseS;
    IfStmt(unique_ptr<Expr> c, unique_ptr<Stmt> t, unique_ptr<Stmt> e=nullptr)
        :cond(move(c)),thenS(move(t)),elseS(move(e)){}
    string gen(TAC& t, SymbolTable& s) override{
        string c=cond->gen(t,s); string Lelse=t.newLabel("L"), Lend=t.newLabel("L");
        if(elseS){ t.emit("ifz",c,"",Lelse); thenS->gen(t,s); t.emit("goto","","",Lend); t.emit("label","","",Lelse); elseS->gen(t,s); t.emit("label","","",Lend); }
        else     { t.emit("ifz",c,"",Lend);  thenS->gen(t,s); t.emit("label","","",Lend); }
        return "";
    }
};
struct WhileStmt: Stmt{
    unique_ptr<Expr> cond; unique_ptr<Stmt> body;
    WhileStmt(unique_ptr<Expr> c, unique_ptr<Stmt> b):cond(move(c)),body(move(b)){}
    string gen(TAC& t, SymbolTable& s) override{
        string Lb=t.newLabel("L"), Lend=t.newLabel("L");
        t.emit("label","","",Lb);
        string c=cond->gen(t,s); t.emit("ifz",c,"",Lend);
        body->gen(t,s);
        t.emit("goto","","",Lb);
        t.emit("label","","",Lend);
        return "";
    }
};

/*=============================*
 * 4) RECURSIVE-DESCENT PARSER *
 *=============================*/
struct Parser {
    Lexer lex; Token cur;
    explicit Parser(const string& src): lex(src) { cur=lex.next(); }
    [[noreturn]] void err(const string& m){ throw runtime_error(m+" at line "+to_string(cur.line)); }
    void eat(Tok t){ if(cur.t==t) cur=lex.next(); else err("Unexpected token: "+cur.lex); }
    bool accept(Tok t){ if(cur.t==t){ cur=lex.next(); return true;} return false; }

    unique_ptr<Expr> factor(){
        if(cur.t==Tok::Num){ int v=stoi(cur.lex); eat(Tok::Num); return make_unique<Num>(v); }
        if(cur.t==Tok::Id){ string n=cur.lex; eat(Tok::Id); return make_unique<Var>(n); }
        if(cur.t==Tok::LParen){ eat(Tok::LParen); auto e=expr(); eat(Tok::RParen); return e; }
        err("Expected factor"); return nullptr;
    }
    unique_ptr<Expr> term(){
        auto e=factor();
        while(cur.t==Tok::Mul || cur.t==Tok::Div){
            string op=(cur.t==Tok::Mul?"*":"/"); eat(cur.t);
            e=make_unique<BinOp>(op, move(e), factor());
        }
        return e;
    }
    unique_ptr<Expr> expr(){
        auto e=term();
        while(cur.t==Tok::Plus || cur.t==Tok::Minus){
            string op=(cur.t==Tok::Plus?"+":"-"); eat(cur.t);
            e=make_unique<BinOp>(op, move(e), term());
        }
        return e;
    }

    unique_ptr<Stmt> statement(){
        if(cur.t==Tok::KwInt){
            eat(Tok::KwInt);
            if(cur.t!=Tok::Id) err("Expected identifier");
            string name=cur.lex; eat(Tok::Id);
            unique_ptr<Expr> init;
            if(accept(Tok::Assign)) init=expr();
            eat(Tok::Semicolon);
            return make_unique<Decl>(name, move(init));
        }
        if(cur.t==Tok::Id){
            string name=cur.lex; eat(Tok::Id); eat(Tok::Assign); auto e=expr(); eat(Tok::Semicolon);
            return make_unique<Assign>(name, move(e));
        }
        if(cur.t==Tok::KwPrint){
            eat(Tok::KwPrint); eat(Tok::LParen); auto e=expr(); eat(Tok::RParen); eat(Tok::Semicolon);
            return make_unique<Print>(move(e));
        }
        if(cur.t==Tok::KwIf){
            eat(Tok::KwIf); eat(Tok::LParen); auto c=expr(); eat(Tok::RParen);
            auto thenS=statement(); unique_ptr<Stmt> elseS;
            if(cur.t==Tok::KwElse){ eat(Tok::KwElse); elseS=statement(); }
            return make_unique<IfStmt>(move(c), move(thenS), move(elseS));
        }
        if(cur.t==Tok::KwWhile){
            eat(Tok::KwWhile); eat(Tok::LParen); auto c=expr(); eat(Tok::RParen);
            auto body=statement();
            return make_unique<WhileStmt>(move(c), move(body));
        }
        if(cur.t==Tok::LBrace){
            eat(Tok::LBrace);
            auto blk=make_unique<Block>();
            while(cur.t!=Tok::RBrace) blk->ss.push_back(statement());
            eat(Tok::RBrace);
            return blk;
        }
        err("Invalid statement"); return nullptr;
    }

    unique_ptr<Block> program(){
        auto blk=make_unique<Block>();
        while(cur.t!=Tok::End) blk->ss.push_back(statement());
        return blk;
    }
};

/*==============================================*
 * 5) DRIVER: COMPILE → TAC (default behavior)  *
 *==============================================*/
static void compile_stdin_to_TAC() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string src((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());
    Parser p(src);
    auto ast = p.program();
    SymbolTable sym(257);
    TAC tac;
    ast->gen(tac, sym);
    cout << "=== TAC ===\n";
    tac.dump(cout);
}

/*==============================================*
 * 6) EXTRA: ASSEMBLY CODE GENERATOR (--asm)     *
 *==============================================*/
static void generate_assembly_from_TAC() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string src((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());

    Parser p(src);
    auto ast = p.program();
    SymbolTable sym(257);
    TAC tac;
    ast->gen(tac, sym);

    cout << "=== PSEUDO ASSEMBLY CODE ===\n";
    for (auto &i : tac.code) {
        if (i.op == "=") cout << "MOV " << i.res << ", " << i.a1 << "\n";
        else if (i.op == "print") cout << "PRINT " << i.a1 << "\n";
        else if (i.op == "label") cout << i.res << ":\n";
        else if (i.op == "goto") cout << "JMP " << i.res << "\n";
        else if (i.op == "ifz") { cout << "CMP " << i.a1 << ", 0\n"; cout << "JE " << i.res << "\n"; }
        else if (i.op == "+") { cout << "MOV R1, " << i.a1 << "\nADD R1, " << i.a2 << "\nMOV " << i.res << ", R1\n"; }
        else if (i.op == "-") { cout << "MOV R1, " << i.a1 << "\nSUB R1, " << i.a2 << "\nMOV " << i.res << ", R1\n"; }
        else if (i.op == "*") { cout << "MOV R1, " << i.a1 << "\nMUL R1, " << i.a2 << "\nMOV " << i.res << ", R1\n"; }
        else if (i.op == "/") { cout << "MOV R1, " << i.a1 << "\nDIV R1, " << i.a2 << "\nMOV " << i.res << ", R1\n"; }
    }
}

/*=============================================================*
 * 7) GRAMMAR TOOLS: FIRST/FOLLOW, LEFT REC., LEFT FACTORING   *
 *=============================================================*/
struct Grammar {
    string start=""; const string EPS="ε";
    // productions[A] = { alt1, alt2, ... } ; alt = vector<symbols>
    unordered_map<string, vector<vector<string>>> P;
    unordered_set<string> nonterm, term;
};
static bool is_nonterm(const Grammar& G, const string& X){ return G.nonterm.count(X); }
static bool is_term(const Grammar& G, const string& X){ return G.term.count(X) || (X!=G.EPS && !is_nonterm(G,X)); }

static unordered_map<string, unordered_set<string>> FIRST(const Grammar& G){
    unordered_map<string, unordered_set<string>> F;
    for(const auto& t: G.term) F[t].insert(t);
    for(const auto& A: G.nonterm) F[A];

    bool changed=true;
    while(changed){
        changed=false;
        for(const auto& pr: G.P){
            const string& A = pr.first;
            for(const auto& alt: pr.second){
                if(alt.empty()){ if(F[A].insert(G.EPS).second) changed=true; continue; }
                bool allEps=true;
                for(size_t i=0;i<alt.size();++i){
                    const string& X = alt[i];
                    if(is_term(G,X)) { if(F[A].insert(X).second) changed=true; allEps=false; break; }
                    for(const auto& a: F[X]) if(a!=G.EPS) if(F[A].insert(a).second) changed=true;
                    if(!F[X].count(G.EPS)){ allEps=false; break; }
                }
                if(allEps) if(F[A].insert(G.EPS).second) changed=true;
            }
        }
        for(const auto& pr: G.P)
            for(const auto& alt: pr.second)
                for(const auto& X: alt)
                    if(is_term(G,X)) F[X].insert(X);
    }
    return F;
}

static unordered_map<string, unordered_set<string>> FOLLOW(
    const Grammar& G,
    const unordered_map<string, unordered_set<string>>& F)
{
    unordered_map<string, unordered_set<string>> Fo;
    for(const auto& A: G.nonterm) Fo[A];
    if(!G.start.empty()) Fo[G.start].insert("$");

    bool changed=true;
    while(changed){
        changed=false;
        for(const auto& pr: G.P){
            const string& A = pr.first;
            for(const auto& alt: pr.second){
                for(size_t i=0;i<alt.size();++i){
                    const string& B = alt[i];
                    if(!is_nonterm(G,B)) continue;
                    unordered_set<string> first_beta;
                    bool eps_all=true;
                    for(size_t j=i+1;j<alt.size();++j){
                        const string& X = alt[j];
                        for(const auto& a: F.at(X)){
                            if(a!=G.EPS) first_beta.insert(a);
                        }
                        if(!F.at(X).count(G.EPS)){ eps_all=false; break; }
                    }
                    for(const auto& a: first_beta) if(Fo[B].insert(a).second) changed=true;
                    if(i+1==alt.size() || eps_all){
                        for(const auto& b: Fo[A]) if(Fo[B].insert(b).second) changed=true;
                    }
                }
            }
        }
    }
    return Fo;
}

static Grammar eliminate_left_recursion(const Grammar& G){
    Grammar H = G;
    vector<string> order(H.nonterm.begin(), H.nonterm.end());
    for(const string& A : order){
        vector<vector<string>> alpha, beta;
        for(const auto& alt: H.P[A]){
            if(!alt.empty() && alt[0]==A) alpha.push_back(vector<string>(alt.begin()+1, alt.end()));
            else beta.push_back(alt);
        }
        if(alpha.empty()) continue;
        string Aprime = A + "'";
        while(H.nonterm.count(Aprime) || H.term.count(Aprime)) Aprime += "'";
        H.nonterm.insert(Aprime);

        vector<vector<string>> newA, newAprime;
        for(auto b: beta){ b.push_back(Aprime); newA.push_back(move(b)); }
        for(auto a: alpha){ a.push_back(Aprime); newAprime.push_back(move(a)); }
        newAprime.push_back({H.EPS});

        H.P[A] = move(newA);
        H.P[Aprime] = move(newAprime);
    }
    return H;
}

static Grammar left_factor(const Grammar& G){
    Grammar H=G;
    bool changed=true;
    while(changed){
        changed=false;
        vector<pair<string, vector<vector<string>>>> items(H.P.begin(), H.P.end());
        for(const auto& kv: items){
            const string& A = kv.first;
            const auto& alts = kv.second;
            unordered_map<string, vector<vector<string>>> groups;
            for(const auto& alt: alts){
                string key = alt.empty()? "<eps>" : alt[0];
                groups[key].push_back(alt);
            }
            for(const auto& g: groups){
                if(g.second.size()<=1) continue;
                vector<string> lcp = g.second[0];
                for(const auto& alt: g.second){
                    size_t k=0; while(k<lcp.size() && k<alt.size() && lcp[k]==alt[k]) k++;
                    lcp.resize(k);
                }
                if(lcp.empty()) continue;
                changed=true;

                string Aprime=A+"'"; while(H.nonterm.count(Aprime)||H.term.count(Aprime)) Aprime+="'";
                H.nonterm.insert(Aprime);

                vector<vector<string>> Anew;
                vector<string> head=lcp; head.push_back(Aprime); Anew.push_back(head);

                vector<vector<string>> AprimeAlts;
                for(const auto& alt: g.second){
                    vector<string> tail(alt.begin()+lcp.size(), alt.end());
                    if(tail.empty()) tail.push_back(H.EPS);
                    AprimeAlts.push_back(move(tail));
                }
                for(const auto& alt: alts){
                    if(find(g.second.begin(), g.second.end(), alt)==g.second.end())
                        Anew.push_back(alt);
                }
                H.P[A]=Anew;
                H.P[Aprime]=AprimeAlts;
                break;
            }
            if(changed) break;
        }
    }
    return H;
}

static void print_grammar(const Grammar& G, const string& title){
    cout << "\n== " << title << " ==\n";
    for(const auto& pr: G.P){
        cout << pr.first << " -> ";
        for(size_t i=0;i<pr.second.size();++i){
            const auto& alt = pr.second[i];
            for(size_t j=0;j<alt.size();++j){
                cout << alt[j] << (j+1<alt.size()? " ":"");
            }
            if(i+1<pr.second.size()) cout << " | ";
        }
        cout << "\n";
    }
}

static void demo_grammar_tools(){
    Grammar G;
    G.start="S";
    G.nonterm={"S","ST","E","T","F"};
    G.term={"+","-","*","/","(",")","id",";","int","=","print"};
    G.P["S"]  = { {"ST"} };
    G.P["ST"] = { {"int","id",";"},
                  {"id","=","E",";"},
                  {"print","(","E",")",";"},
                  {"ST","ST"} };
    G.P["E"]  = { {"E","+","T"}, {"E","-","T"}, {"T"} };
    G.P["T"]  = { {"T","*","F"}, {"T","/","F"}, {"F"} };
    G.P["F"]  = { {"(","E",")"}, {"id"} };

    print_grammar(G, "Original Grammar");
    auto Fst = FIRST(G);
    cout << "\nFIRST sets:\n";
    for(const auto& kv: Fst){ if(!is_nonterm(G,kv.first)) continue;
        cout<< "FIRST("<<kv.first<<") = { ";
        bool first=true; for(const auto& a: kv.second){ if(!first) cout<<", "; cout<<a; first=false; }
        cout << " }\n";
    }
    auto Fol = FOLLOW(G, Fst);
    cout << "\nFOLLOW sets:\n";
    for(const auto& kv: Fol){
        cout<< "FOLLOW("<<kv.first<<") = { ";
        bool first=true; for(const auto& a: kv.second){ if(!first) cout<<", "; cout<<a; first=false; }
        cout << " }\n";
    }

    auto G1 = eliminate_left_recursion(G);
    print_grammar(G1, "After Left Recursion Elimination");

    auto G2 = left_factor(G1);
    print_grammar(G2, "After Left Factoring");
}

/*=============================*
 * 8) main(): mode dispatcher  *
 *=============================*/
int main(int argc, char** argv){
    try{
        if(argc>1 && string(argv[1])=="--demo-grammar"){
            demo_grammar_tools();
        } else if(argc>1 && string(argv[1])=="--asm"){
            generate_assembly_from_TAC();
        } else {
            compile_stdin_to_TAC();
        }
    } catch(const exception& e){
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
