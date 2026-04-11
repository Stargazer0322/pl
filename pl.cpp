#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <variant>
#include <stdexcept>
#include <cstdio>
#include <map>

using namespace std;

// 定義 Token 的種類
enum Token_Type {
    Symbol,         // 符號 (例如變數名稱、函式名稱)
    Int,            // 整數數值
    Float,          // 浮點數數值
    String,         // 字串數值
    Nil,            // 空串列或布林假值 (nil, #f, ())
    T,              // 布林真值 (#t)
    LeftParen,      // 左括號 '('
    RightParen,     // 右括號 ')'
    Dot,            // 點 '.' (用於 Dotted Pair)
    Quote,          // 單引號 '\'' (用於 Quote 語法)
    EndOfFile,      // 檔案結尾 (EOF)
    ErrorToken,     // 發生錯誤時的無效 Token
    Primitive       // 內建函式 (例如 +, -, car 等等)
};

// Token 結構
struct Token {
    Token_Type type;                   // Token 的種類
    string original_value;             // 程式碼中原始的字串內容
    int line;                          // 所在的邏輯行號
    int col;                           // 所在的邏輯欄位 (Column)
    variant<int, float, string> value; // 轉型後的實際數值 (整數、浮點數或字串)

    // 建構子：預設建立 ErrorToken
    Token() : type(ErrorToken), line(0), col(0) {}
    // 建構子：根據傳入的 Token_Type 與字串自動轉換出對應的 value 型態
    Token(Token_Type t, string s) : type(t), original_value(s), line(0), col(0) {
        if (t == Int) value = stoi(s);
        else if (t == Float) value = stof(s);
        else value = s;
    }
    Token(Token_Type t, string s, int l, int c) : type(t), original_value(s), line(l), col(c) {
        if (t == Int) value = stoi(s);
        else if (t == Float) value = stof(s);
        else value = s;
    }
};

// AST (抽象語法樹) 節點結構
struct Node {
    bool is_atom;
    Token token;    // 若為 Atom，存放 Token
    Node* left;     // 若為 List/Pair，存放左指標 (car)
    Node* right;    // 若為 List/Pair，存放右指標 (cdr)

    // 建構子：建立 Error 節點
    Node() : is_atom(false), left(nullptr), right(nullptr) {}

    // 建構子：建立 Atom 節點
    Node(Token t) : is_atom(true), token(t), left(nullptr), right(nullptr) {}
    
    // 建構子：建立 Pair 節點
    Node(Node* l, Node* r) : is_atom(false), left(l), right(r) {}
};

static const string prims[] = {
    "+", "-", "*", "/", "=", "<", ">", "<=", ">=", 
    "cons", "car", "cdr", "list", "pair?", "null?", 
    "integer?", "real?", "number?", "symbol?", "string?", 
    "boolean?", "atom?", "eqv?", "equal?", "not", "string-append", 
    "string>?", "string<?", "string=?", "exit"
};

// 定義 Parser 錯誤的種類
enum ParserError_Type {
    no_closing_quote,       // 字串缺乏右雙引號閉合
    no_more_input,          // 預期還有輸入卻遇到 EOF
    unexpected_token_atom,  // 語法錯誤：預期為 Atom 或是左括號 '('
    unexpected_token_paren, // 語法錯誤：預期為右括號 ')'
};

// 定義 Eval 錯誤的種類
enum EvalError_Type {
    unbound_symbol,             // 變數未定義 (未在 environment 找到)
    non_list,                   // 嘗試對非串列進行操作 (例如傳入非正規 list 給函式)
    incorrect_num_of_args,      // 傳入的參數數量錯誤
    incorrect_arg_type,         // 傳入的參數型態錯誤 (例如 car 遇到數字、+ 遇到字串)
    apply_non_function,         // 嘗試將非函式型態的值當作函式呼叫
    no_return_value,            // 條件分支 (if 或 cond) 執行後沒有可回傳的值
    division_by_zero,           // 發生除以零的計算錯誤
    define_format,              // define 語法格式錯誤
    cond_format,                // cond 語法格式錯誤
    level_of_clean_environment, // clean-environment 不在最外層 (Top-level) 被呼叫
    level_of_define,            // define 不在最外層 (Top-level) 被呼叫
    level_of_exit,              // exit 不在最外層 (Top-level) 被呼叫
    lambda_format               // lambda 語法格式錯誤
};

// 記錄錯誤的 Exception 結構
struct ParseError : public exception {
    ParserError_Type type; // 解析錯誤的種類
    int line;              // 發生錯誤的行號
    int col;               // 發生錯誤的欄位
    string token_str;      // 造成錯誤的 Token 原始字串
    ParseError(ParserError_Type t, int l, int c, string s) : type(t), line(l), col(c), token_str(s){}
};

// Eval 專用的 Exception
struct EvalError : public exception {
    EvalError_Type type; // 評估錯誤的種類
    string msg;          // 附加訊息或操作符名稱 (例如 "a", "+", "car")
    Node* err_node;      // 發生錯誤時，需要被 PrintSExp 印出來的語法樹節點

    EvalError(EvalError_Type t) : type(t), msg(""), err_node(nullptr) {}

    EvalError(EvalError_Type t, string m) : type(t), msg(m), err_node(nullptr) {}

    EvalError(EvalError_Type t, string m, Node* n) : type(t), msg(m), err_node(n) {}
};

// 刪除整個樹
void FreeTree(Node* node) {
    if (!node) return;
    if (!node->is_atom) {
        FreeTree(node->left);
        FreeTree(node->right);
    }
    delete node;
}

// 刪除部分樹
void FreeTree(Node* node, Node* unfree) {
    if (!node) return;
    if (node == unfree) return;
    if (!node->is_atom) {
        FreeTree(node->left, unfree);
        FreeTree(node->right, unfree);
    }
    delete node;
}

// 複製整個樹 (用於將值存入環境變數，避免與指令一起被 FreeTree 釋放)
Node* CloneTree(Node* node) {
    if (!node) return nullptr;
    if (node->is_atom) return new Node(node->token);
    return new Node(CloneTree(node->left), CloneTree(node->right));
}

class Environment {
public:
    map<string, Node*> vars;
    Environment* parent;

    Environment(Environment* p = nullptr) : parent(p) {}


    // 尋找變數：先找自己，找不到再往上找 parent
    Node* LookupVar(const string& name) {
        if (vars.count(name)) return vars[name];
        if (parent != nullptr) return parent->LookupVar(name);
        return nullptr;
    }

    bool LookupNode(Node* node) {
        for (auto& x : vars) {
            if (x.second == node) {
                return true;
            }
        }
        if (parent != nullptr) return parent->LookupNode(node);
        return false;
    }

    // 綁定變數 (用於 define 或函數傳參)
    void Define(const string& name, Node* val) {
        vars[name] = val;
    }
};

// Scanner 類別 (Lexer)
class Scanner {
private:
    string line_str;
    int physical_line = 0;       // 追蹤實際讀取的實體行數
    int start_physical_line = 0; // 追蹤上一個 S-exp 結束時所在的實體行數
    int logical_line = 1;        // 要印出來的邏輯行號
    int logical_col = 1;         // 要印出來的邏輯欄位
    int pos = 0;
    bool eof_reached = false;

    // 讀取新的一行
    void LoadNextLine() {
        if (!getline(cin, line_str)) {
            eof_reached = true;
            return;
        }
        line_str += '\n'; // 補回換行字元
        physical_line++;
        pos = 0;
    }

    // 判斷字串是否為合法的整數格式 (可帶有正負號)
    bool IsInt(string s) {
        if (s.empty()) return false;
        int start = 0;
        if (s[0] == '+' || s[0] == '-') {
            if (s.length() == 1) return false;
            start = 1;
        }
        for (int i = start; i < s.length(); i++) {
            if (!isdigit(s[i])) return false;
        }
        return true;
    }

    // 判斷字串是否為合法的浮點數格式 (包含單一小數點)
    bool IsFloat(string s) {
        if (s.empty()) return false;
        int start = 0;
        if (s[0] == '+' || s[0] == '-') {
            if (s.length() == 1) return false;
            start = 1;
        }
        int dot_count = 0;
        int digit_count = 0;
        for (int i = start; i < s.length(); i++) {
            if (s[i] == '.') dot_count++;
            else if (isdigit(s[i])) digit_count++;
            else return false;
        }
        return (dot_count == 1 && digit_count > 0);
    }

    // 處理字串內的跳脫字元 (如 \n, \t, \", \\, \')
    string ProcessString(string raw) {
        string res = "";
        for (int i = 0; i < raw.length(); i++) {
            if (raw[i] == '\\' && i + 1 < raw.length()) {
                char next = raw[i + 1];
                if (next == 'n') { res += '\n'; i++; }
                else if (next == 't') { res += '\t'; i++; }
                else if (next == '"') { res += '"'; i++; }
                else if (next == '\\') { res += '\\'; i++; }
                else if (next == '\'') { res += '\''; i++; }
                else { res += raw[i]; }
            } else {
                res += raw[i];
            }
        }
        return res;
    }

public:
    bool is_new_sexp = true; // 標記是否正在等待新的 S-exp 的第一個字元

    Scanner() { 
        LoadNextLine(); 
        ReadyForNewSExp(); 
    }

    // 當一個 S-exp 讀取完畢，準備迎接下一個時呼叫
    void ReadyForNewSExp() {
        is_new_sexp = true;
        start_physical_line = physical_line; // 紀錄上一個 S-exp 結束在哪個實體行
        logical_line = 1;
        logical_col = 1;
    }

    // 放棄這整行 (發生 error 時)
    void DiscardRestOfLine() {
        pos = line_str.length();
    }

    // 取得下一個 Token，實作語法分析器的核心邏輯 (包含跳過空白、註解及字串處理)
    Token GetNextToken() {
        while (true) {
            if (eof_reached) return Token(EndOfFile, "", -1, -1);

            if (pos >= line_str.length()) {
                LoadNextLine();
                continue;
            }

            char c = line_str[pos];

            // 1. 跳過空白字元與換行
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (c == '\n') {
                    // 只有當換行不是發生在上一個 S-exp 結束的那一行時，才增加邏輯行號
                    if (is_new_sexp) {
                        if (physical_line != start_physical_line) {
                            logical_line++;
                        }
                    } else {
                        logical_line++;
                    }
                    logical_col = 1; // 換行後 Column 永遠歸 1
                } else {
                    logical_col++; // 空格/Tab 只增加 Column
                }
                pos++;
                continue;
            }

            // 2. 跳過註解 (直到遇到換行，但不吃掉換行，留給下一輪迴圈處理 Column 重置)
            if (c == ';') {
                while (pos < line_str.length() && line_str[pos] != '\n') {
                    pos++;
                    logical_col++;
                }
                continue; 
            }
            
            // 如果還在等待新 S-exp，鎖定第一行
            if (is_new_sexp) {
                is_new_sexp = false;
            }

            int start_col = logical_col;
            int start_line = logical_line;

            // 3. 處理單一字元 Token
            if (c == '(') { pos++; logical_col++; return Token(LeftParen, "(", start_line, start_col); }
            if (c == ')') { pos++; logical_col++; return Token(RightParen, ")", start_line, start_col); }
            if (c == '\'') { pos++; logical_col++; return Token(Quote, "'", start_line, start_col); }

            // 4. 處理字串 (String)
            if (c == '"') {
                string raw = "\"";
                pos++; 
                logical_col++;
                bool closed = false;
                while (pos < line_str.length()) {
                    char sc = line_str[pos];
                    if (sc == '\n' || sc == '\r') {
                        // 字串未閉合錯誤：回傳讀到換行時的 logical_line 與 logical_col
                        throw ParseError(no_closing_quote, logical_line, logical_col, "");
                    }
                    if (sc == '\\') {
                        raw += sc; 
                        pos++; 
                        logical_col++;
                        if (pos < line_str.length() && line_str[pos] != '\n' && line_str[pos] != '\r') {
                            raw += line_str[pos]; 
                            pos++; 
                            logical_col++;
                        }
                        continue;
                    }
                    if (sc == '"') {
                        raw += '"'; 
                        pos++; 
                        logical_col++;
                        closed = true;
                        break;
                    }
                    raw += sc; 
                    pos++; 
                    logical_col++;
                }
                if (!closed) throw ParseError(no_closing_quote, logical_line, logical_col, "");
                return Token(String, ProcessString(raw), start_line, start_col);
            }

            // 5. 處理連續字元 Token
            string seq = "";
            while (pos < line_str.length()) {
                char sc = line_str[pos];
                if (sc == ' ' || sc == '\t' || sc == '\n' || sc == '\r' ||
                    sc == '(' || sc == ')' || sc == '\'' || sc == '"' || sc == ';') {
                    break;
                }
                seq += sc;
                pos++; 
                logical_col++;
            }

            if (seq == ".") return Token(Dot, ".", start_line, start_col);
            if (seq == "t" || seq == "#t") return Token(T, "#t", start_line, start_col);
            if (seq == "nil" || seq == "#f" || seq == "()") return Token(Nil, "nil", start_line, start_col);
            if (IsInt(seq)) return Token(Int, seq, start_line, start_col);
            if (IsFloat(seq)) return Token(Float, seq, start_line, start_col);

            return Token(Symbol, seq, start_line, start_col);
        }
    }
};


// Parser 類別 (語法分析器)
class Parser {
private:
    Scanner scanner;
    Token peek_token;
    bool has_peek = false;

    // 取得下一個 Token (若有暫存則回傳暫存的 Token)
    Token GetNext() {
        if (has_peek) {
            has_peek = false;
            return peek_token;
        }
        return scanner.GetNextToken();
    }

    // 偷看下一個 Token，但不將其從串流中消耗掉
    Token PeekNext() {
        if (!has_peek) {
            peek_token = scanner.GetNextToken();
            has_peek = true;
        }
        return peek_token;
    }

public:
    // 通知 Scanner 準備讀取新的 S-Expression
    void ReadyForNewSExp() {
        scanner.ReadyForNewSExp();
    }

    // 發生語法錯誤時，捨棄當前行剩餘的字元與暫存的 Token
    void DiscardLine() {
        scanner.DiscardRestOfLine();
        has_peek = false; 
    }

    // 遞迴讀取一個 S-Expression (Atom 或是 Pair/List)
    Node* ReadSExp() {
        Token t = GetNext();
        if (t.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");

        if (t.type == RightParen || t.type == Dot) {
            throw ParseError(unexpected_token_atom, t.line, t.col, t.original_value);
        }

        if (t.type == LeftParen) {
            Token p = PeekNext();
            if (p.type == Dot) {
                throw ParseError(unexpected_token_atom, p.line, p.col, p.original_value);
            }
            return ReadList();
        }

        if (t.type == Quote) {
            Node* inner = ReadSExp();
            Node* q = new Node(Token(Symbol, "quote", t.line, t.col));
            Node* pair2 = new Node(inner, new Node(Token(Nil, "nil", t.line, t.col)));
            return new Node(q, pair2);
        }

        return new Node(t);
    }

    // 遞迴讀取 List 結構，處理括號內的元素以及 Dotted Pair
    Node* ReadList() {
        Token t = PeekNext();
        if (t.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");

        if (t.type == RightParen) {
            GetNext();
            return new Node(Token(Nil, "nil", t.line, t.col));
        }

        if (t.type == Dot) {
            GetNext();
            Node* snn = ReadSExp();
            Token p = GetNext();
            if (p.type != RightParen) {
                if (p.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");
                throw ParseError(unexpected_token_paren, p.line, p.col, p.original_value);
            }
            return snn;
        }

        Node* left = ReadSExp();
        Node* right = ReadList();
        return new Node(left, right);
    }
};


// Eval 類別
class Evaluator {
private:
    Environment curr_env;
    string special_forms [8];
    
    // 計算 List 的長度，若遇到非正規 List (未以 nil 結尾) 則回傳 -1
    int ListLength(Node* list) {
        int count = 0;
        while (list != nullptr) {
            if (list->is_atom) {
                if (list->token.type == Nil) return count;
                return -1;
            }
            count++;
            list = list->right;
        }
        return count;
    }

    // 取得串列中的最後一個元素 (car)
    // 用途：用於取得 begin 等序列執行後，最後一個表達式的回傳值
    Node* GetLastList(Node* list) {
        if (list == nullptr || (list->is_atom && list->token.type == Nil)) {
            return list;
        }
        Node* current = list;
        while (current->right != nullptr && !(current->right->is_atom && current->right->token.type == Nil)) {
            current = current->right;
        }
        return current->left;
    }

    // 深度比對兩個節點結構及其內容是否完全相等 (用於 equal?)
    bool IsEqualNode(Node* a, Node* b) {
        // 若指標相同，直接回傳 true (同一個物件一定相等)
        if (a == b) return true;
        // 若其中一個為 nullptr，則不相等
        if (a == nullptr || b == nullptr) return false;
        
        if (a->is_atom && b->is_atom) {
            // 型別不同則不相等
            if (a->token.type != b->token.type) return false;
            
            // 根據不同型別比對存放的實際值
            if (a->token.type == Int) return get<int>(a->token.value) == get<int>(b->token.value);
            if (a->token.type == Float) return get<float>(a->token.value) == get<float>(b->token.value);
            if (a->token.type == String || a->token.type == Symbol) return get<string>(a->token.value) == get<string>(b->token.value);
            if (a->token.type == Nil || a->token.type == T) return true;
            
            return a->token.original_value == b->token.original_value;
        }
        
        // 如果都不是 Atom (也就是都是 List/Pair)，則遞迴比對 left(car) 與 right(cdr)
        if (!a->is_atom && !b->is_atom) {
            return IsEqualNode(a->left, b->left) && IsEqualNode(a->right, b->right);
        }
        
        // 一個是 Atom，一個是 Pair 的情況
        return false;
    }

    // 建立一個 Pair 節點 (將 car 與 cdr 連接起來)
    Node* Cons(Node* car, Node* cdr) {
        Node* new_node = new Node(car, cdr);
        return new_node;
    }

    // --- 建立各類型基礎 Node 的函式 ---

    // 建立整數節點
    Node* CreateIntNode(int val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Int, to_string(val));
        return n;
    }

    // 建立浮點數節點
    Node* CreateFloatNode(float val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Float, to_string(val));
        return n;
    }

    // 建立 Nil 節點 (#f / nil / ())
    Node* CreateNilNode() {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Nil, "nil");
        return n;
    }

    // 建立 True 節點 (#t)
    Node* CreateTrueNode() {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(T, "#t");
        return n;
    }

    // 建立字串節點
    Node* CreateStringNode(string val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(String, val);
        return n;
    }

    // 建立內建函式 (Primitive) 節點
    Node* CreatePrimitiveNode(string name) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Primitive, name);
        return n;
    }

    // --- 內建操作 (Primitives) 評估函式 ---

    // 評估 car：取得 List 的第一個元素 (left)
    Node* EvalCar(Node* args) {
        if (args->is_atom) {
            throw EvalError(incorrect_arg_type, "car", args);
        }
        // 檢查參數個數
        if (ListLength(args) != 1) {
            throw EvalError(incorrect_num_of_args, "car");
        }
        Node* arg_val = args->left;
        // 檢查是否為 list (pair)
        if (arg_val->is_atom) {
            throw EvalError(incorrect_arg_type, "car", arg_val); // 這裡傳入 atom 供後續列印
        }
        return arg_val->left;
    }

    // 評估 cdr：取得 List 的剩餘元素 (right)
    Node* EvalCdr(Node* args) {
        if (args->is_atom) {
            throw EvalError(incorrect_arg_type, "cdr", args);
        }
        // 檢查參數個數
        if (ListLength(args) != 1) {
            throw EvalError(incorrect_num_of_args, "cdr");
        }
        Node* arg_val = args->left;
        // 檢查是否為 list (pair)
        if (arg_val->is_atom) {
            throw EvalError(incorrect_arg_type, "cdr", arg_val); // 這裡傳入 atom 供後續列印
        }
        return arg_val->right;
    }

    // 評估加法 (+)：支援多個參數，若包含浮點數則結果轉為浮點數
    Node* EvalAdd(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "+", arg_val);
            }

            if (arg_val->token.type == Float) {
                if (!is_float) { 
                    is_float = true; 
                    f_sum = i_sum; 
                }
                f_sum += get<float>(arg_val->token.value);
            } else {
                if (is_float) {
                    f_sum += get<int>(arg_val->token.value);
                } else {
                    i_sum += get<int>(arg_val->token.value);
                }
            }
            current = current->right; // 走到下一個算好的參數
        }
        
        if (is_float) return CreateFloatNode(f_sum);
        return CreateIntNode(i_sum);
    }

    // 評估減法 (-)：支援多個參數
    Node* EvalSub(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "-", arg_val);
            }
            
            if (arg_val->token.type == Float) {
                is_float = true;
                f_sum = get<float>(arg_val->token.value);
            } else {
                i_sum = get<int>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "-", arg_val);
            }
            
            if (arg_val->token.type == Float) {
                if (!is_float) { 
                    is_float = true; 
                    f_sum = i_sum; 
                }
                f_sum -= get<float>(arg_val->token.value);
            } else {
                if (is_float) {
                    f_sum -= get<int>(arg_val->token.value);
                } else {
                    i_sum -= get<int>(arg_val->token.value);
                }
            }
            current = current->right; // 走到下一個算好的參數
        }
        
        if (is_float) return CreateFloatNode(f_sum);
        return CreateIntNode(i_sum);
    }

    // 評估乘法 (*)：支援多個參數
    Node* EvalMul(Node* args) {
        bool is_float = false;
        float f_sum = 1.0f; // 乘法初始值修正為 1
        int i_sum = 1;
        Node* current = args;
        
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "*", arg_val);
            }
            
            if (arg_val->token.type == Float) {
                if (!is_float) { 
                    is_float = true; 
                    f_sum = i_sum; 
                }
                f_sum *= get<float>(arg_val->token.value);
            } else {
                if (is_float) {
                    f_sum *= get<int>(arg_val->token.value);
                } else {
                    i_sum *= get<int>(arg_val->token.value);
                }
            }
            current = current->right; // 走到下一個算好的參數
        }
        
        if (is_float) return CreateFloatNode(f_sum);
        return CreateIntNode(i_sum);
    }

    // 評估除法 (/)：支援多個參數，並檢查除以零的錯誤
    Node* EvalDiv(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "/", arg_val);
            }
            
            if (arg_val->token.type == Float) {
                is_float = true;
                f_sum = get<float>(arg_val->token.value);
            } else {
                i_sum = get<int>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "/", arg_val);
            }
            
            if (arg_val->token.type == Float) {
                if (!is_float) { 
                    is_float = true; 
                    f_sum = i_sum; 
                }
                f_sum /= get<float>(arg_val->token.value);
            } else {
                if (is_float) {
                    f_sum /= get<int>(arg_val->token.value);
                } else {
                    int val = get<int>(arg_val->token.value);
                    if (val != 0) { // 避免除以 0 崩潰
                        i_sum /= val;
                    } else {
                        throw EvalError(division_by_zero);
                    }
                }
            }
            current = current->right; // 走到下一個算好的參數
        }
        
        if (is_float) return CreateFloatNode(f_sum);
        return CreateIntNode(i_sum);
    }

    // 評估數值相等 (=)：判斷相鄰參數是否全部相等
    Node* EvalEqu(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || (check_curr->left->token.type != Int && check_curr->left->token.type != Float)) {
                throw EvalError(incorrect_arg_type, "=", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "=", arg_val);
            }
            if (arg_val->token.type == Float) {
                prev_is_float = true;
                f_prev = get<float>(arg_val->token.value);
            } else {
                i_prev = get<int>(arg_val->token.value);
                f_prev = i_prev;
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "=", arg_val);
            }
            float f_curr = 0.0f;
            int i_curr = 0;
            bool curr_is_float = false;

            if (arg_val->token.type == Float) {
                curr_is_float = true;
                f_curr = get<float>(arg_val->token.value);
            } else {
                i_curr = get<int>(arg_val->token.value);
                f_curr = i_curr;
            }

            if (prev_is_float || curr_is_float) {
                if (f_prev < f_curr || f_prev > f_curr) return CreateNilNode(); 
            } else {
                if (i_prev < i_curr || i_prev > i_curr) return CreateNilNode();
            }

            prev_is_float = curr_is_float;
            f_prev = f_curr;
            i_prev = i_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 評估數值小於 (<)：判斷參數是否嚴格遞增
    Node* EvalLess(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || (check_curr->left->token.type != Int && check_curr->left->token.type != Float)) {
                throw EvalError(incorrect_arg_type, "<", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "<", arg_val);
            }
            if (arg_val->token.type == Float) {
                prev_is_float = true;
                f_prev = get<float>(arg_val->token.value);
            } else {
                i_prev = get<int>(arg_val->token.value);
                f_prev = i_prev;
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "<", arg_val);
            }
            float f_curr = 0.0f;
            int i_curr = 0;
            bool curr_is_float = false;

            if (arg_val->token.type == Float) {
                curr_is_float = true;
                f_curr = get<float>(arg_val->token.value);
            } else {
                i_curr = get<int>(arg_val->token.value);
                f_curr = i_curr;
            }

            if (prev_is_float || curr_is_float) {
                if (f_prev >= f_curr) return CreateNilNode(); 
            } else {
                if (i_prev >= i_curr) return CreateNilNode();
            }

            prev_is_float = curr_is_float;
            f_prev = f_curr;
            i_prev = i_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 評估數值大於 (>)：判斷參數是否嚴格遞減
    Node* EvalGreater(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || (check_curr->left->token.type != Int && check_curr->left->token.type != Float)) {
                throw EvalError(incorrect_arg_type, ">", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, ">", arg_val);
            }
            if (arg_val->token.type == Float) {
                prev_is_float = true;
                f_prev = get<float>(arg_val->token.value);
            } else {
                i_prev = get<int>(arg_val->token.value);
                f_prev = i_prev;
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, ">", arg_val);
            }
            float f_curr = 0.0f;
            int i_curr = 0;
            bool curr_is_float = false;

            if (arg_val->token.type == Float) {
                curr_is_float = true;
                f_curr = get<float>(arg_val->token.value);
            } else {
                i_curr = get<int>(arg_val->token.value);
                f_curr = i_curr;
            }

            if (prev_is_float || curr_is_float) {
                if (f_prev <= f_curr) return CreateNilNode(); 
            } else {
                if (i_prev <= i_curr) return CreateNilNode();
            }

            prev_is_float = curr_is_float;
            f_prev = f_curr;
            i_prev = i_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 評估數值大於等於 (>=)：判斷參數是否非遞增
    Node* EvalGreaterEqual(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || (check_curr->left->token.type != Int && check_curr->left->token.type != Float)) {
                throw EvalError(incorrect_arg_type, ">=", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, ">=", arg_val);
            }
            if (arg_val->token.type == Float) {
                prev_is_float = true;
                f_prev = get<float>(arg_val->token.value);
            } else {
                i_prev = get<int>(arg_val->token.value);
                f_prev = i_prev;
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, ">=", arg_val);
            }
            float f_curr = 0.0f;
            int i_curr = 0;
            bool curr_is_float = false;

            if (arg_val->token.type == Float) {
                curr_is_float = true;
                f_curr = get<float>(arg_val->token.value);
            } else {
                i_curr = get<int>(arg_val->token.value);
                f_curr = i_curr;
            }

            if (prev_is_float || curr_is_float) {
                if (f_prev < f_curr) return CreateNilNode(); 
            } else {
                if (i_prev < i_curr) return CreateNilNode();
            }

            prev_is_float = curr_is_float;
            f_prev = f_curr;
            i_prev = i_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 評估數值小於等於 (<=)：判斷參數是否非遞減
    Node* EvalLessEqual(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || (check_curr->left->token.type != Int && check_curr->left->token.type != Float)) {
                throw EvalError(incorrect_arg_type, "<=", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "<=", arg_val);
            }
            if (arg_val->token.type == Float) {
                prev_is_float = true;
                f_prev = get<float>(arg_val->token.value);
            } else {
                i_prev = get<int>(arg_val->token.value);
                f_prev = i_prev;
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                throw EvalError(incorrect_arg_type, "<=", arg_val);
            }
            float f_curr = 0.0f;
            int i_curr = 0;
            bool curr_is_float = false;

            if (arg_val->token.type == Float) {
                curr_is_float = true;
                f_curr = get<float>(arg_val->token.value);
            } else {
                i_curr = get<int>(arg_val->token.value);
                f_curr = i_curr;
            }

            if (prev_is_float || curr_is_float) {
                if (f_prev > f_curr) return CreateNilNode(); 
            } else {
                if (i_prev > i_curr) return CreateNilNode();
            }

            prev_is_float = curr_is_float;
            f_prev = f_curr;
            i_prev = i_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 評估 cons：將兩個元素結合成一個 Pair
    Node* EvalCons(Node* args) {
        if (ListLength(args) != 2) {
            throw EvalError(incorrect_num_of_args, "cons");
        }
        Node* current = args;
        Node* car_val = current->left;
        if (car_val == nullptr) { 
            //throw EvalError("ERROR (+ with incorrect argument type)");
        }
        Node* cdr_val = current->right->left;
        if (cdr_val == nullptr) {
            //throw EvalError("ERROR (+ with incorrect argument type)");
        }
        return Cons(car_val, cdr_val);
    }
    
    // 評估 list：將多個元素組成一個以 Nil 結尾的正規串列
    Node* EvalList(Node* args) {
        if (args == nullptr || args->token.type == Nil) {
            return args; // 到底了，回傳 Nil
        }
        Node* evaluated_car = Eval(args->left);       // 算左邊的單一參數
        Node* evaluated_cdr = EvalList(args->right);  // 遞迴處理剩下的串列
        return Cons(evaluated_car, evaluated_cdr);    // 重新組裝回傳
    }

    // 判斷是否為 Pair (包含 List，但不含 ()/nil)
    Node* EvalPair(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode(); 
        }
        
        Node* target = args->left; 
        
        if (target != nullptr && !target->is_atom) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為 Null (即 nil 或 ())
    Node* EvalNull(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && target->token.type == Nil) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為整數 (integer?)
    Node* EvalInteger(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && target->token.type == Int) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為實數 (real?，包含整數與浮點數)
    Node* EvalReal(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && (target->token.type == Int || target->token.type == Float)) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為數字 (number?，在目前的實作中等同於 real?)
    Node* EvalNumber(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && (target->token.type == Int || target->token.type == Float)) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為符號 (symbol?)
    Node* EvalSymbol(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && target->token.type == Symbol) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為字串 (string?)
    Node* EvalString(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && target->token.type == String) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為布林值 (boolean?，即 #t 或 #f/nil)
    Node* EvalBoolean(Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateNilNode();
        }
        
        Node* target = args->left;
        
        if (target != nullptr && target->is_atom && (target->token.type == Nil || target->token.type == T)) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }

    // 判斷是否為 Atom (除了 Pair 以外的任何元素，包含 nil)
    Node* EvalAtom (Node* args) {
        if (args == nullptr || args->is_atom || args->token.type == Nil) {
            return CreateTrueNode();
        }
        
        Node* target = args->left; 
        
        if (target != nullptr && !target->is_atom) {
            return CreateNilNode();
        }
        return CreateTrueNode();
    }

    // 判斷兩個元素在值上是否等價 (eqv?)
    Node* EvalEqv(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateNilNode();
        
        Node* first_arg = args->left; 
        
        Node* remaining = args->right;
        if (remaining == nullptr || remaining->token.type == Nil) return CreateNilNode();
        Node* second_arg = remaining->left; 
        
        // 1. 若指標相同 (同一個物件，包含同一個 list 的別名)，直接回傳 true
        if (first_arg == second_arg) return CreateTrueNode();
        
        if (first_arg == nullptr || second_arg == nullptr) return CreateNilNode();

        // 2. 針對 Atom，檢查其值是否相等 (Scheme 中 eqv? 對於基本型別會判斷值)
        if (first_arg->is_atom && second_arg->is_atom) {
            if (first_arg->token.type != second_arg->token.type) return CreateNilNode();
            
            Token_Type t = first_arg->token.type;
            if (t == Int) return get<int>(first_arg->token.value) == get<int>(second_arg->token.value) ? CreateTrueNode() : CreateNilNode();
            if (t == Float) return get<float>(first_arg->token.value) == get<float>(second_arg->token.value) ? CreateTrueNode() : CreateNilNode();
            if (t == Symbol) return get<string>(first_arg->token.value) == get<string>(second_arg->token.value) ? CreateTrueNode() : CreateNilNode();
            if (t == String) return CreateNilNode();
            if (t == Nil || t == T) return CreateTrueNode();
        }
        
        return CreateNilNode();
    }

    // 判斷兩個元素的結構與內容是否完全相同 (equal?)
    Node* EvalEqual(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateNilNode();
        
        // 取得第一個參數
        Node* first_arg = args->left; 
        
        // 取得第二個參數的串列結構
        Node* remaining = args->right;
        if (remaining == nullptr || remaining->token.type == Nil) return CreateNilNode();
        Node* second_arg = remaining->left; 
        
        // 執行結構比對
        if (IsEqualNode(first_arg, second_arg)) {
            return CreateTrueNode();
        }

        return CreateNilNode();
    }

    // 邏輯非 (not)：若參數為 #f/nil 則回傳 #t，否則回傳 #f
    Node* EvalNot(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateNilNode();

        Node* target = args->left;

        if (target != nullptr && target->is_atom && target->token.type == Nil) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }
    
    // 字串串接 (string-append)
    Node* EvalStringAppend(Node* args) {
        Node* current = args;
        string str = "";
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string-append", arg_val);
            }

            string temp = get<string>(arg_val->token.value);
            temp.erase(0, 1);
            temp.pop_back();
            str += temp;
            current = current->right; // 走到下一個算好的參數
        }
        str.insert(0, "\"");
        str += "\"";
        return CreateStringNode(str);
    }

    // 判斷字串是否嚴格大於 (string>?)
    Node* EvalStringGreater(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || check_curr->left->token.type != String) {
                throw EvalError(incorrect_arg_type, "string>?", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string>?", arg_val);
            }
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string>?", arg_val);
            }
            string str_curr = "";
            if (arg_val->token.type == String) {
                str_curr = get<string>(arg_val->token.value);
            }

            if (str_prev <= str_curr) return CreateNilNode();
            str_prev = str_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 判斷字串是否嚴格小於 (string<?)
    Node* EvalStringLess(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || check_curr->left->token.type != String) {
                throw EvalError(incorrect_arg_type, "string<?", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string<?", arg_val);
            }
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left;
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string<?", arg_val);
            } 
            string str_curr = "";
            if (arg_val->token.type == String) {
                str_curr = get<string>(arg_val->token.value);
            }

            if (str_prev >= str_curr) return CreateNilNode();
            str_prev = str_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // 判斷字串是否相等 (string=?)
    Node* EvalStringEqual(Node* args) {
        Node* check_curr = args;
        while (check_curr != nullptr && check_curr->token.type != Nil) {
            if (check_curr->left == nullptr || check_curr->left->token.type != String) {
                throw EvalError(incorrect_arg_type, "string=?", check_curr->left);
            }
            check_curr = check_curr->right;
        }

        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string=?", arg_val);
            }
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left;
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                throw EvalError(incorrect_arg_type, "string=?", arg_val);
            } 
            string str_curr = "";
            if (arg_val->token.type == String) {
                str_curr = get<string>(arg_val->token.value);
            }

            if (str_prev > str_curr || str_prev < str_curr) return CreateNilNode();
            str_prev = str_curr;

            current = current->right; 
        }
        
        return CreateTrueNode();
    }

    // --- Evaluator 核心邏輯：Apply 與 Special Forms 處理 ---

    // Apply：將算好的參數套用到指定的內建函式 (Primitive) 上
    Node* Apply(Node* op, Node* args) {
        if (op->is_atom && op->token.type == Primitive) {
            string op_name = get<string>(op->token.value);
            if (op_name == "+") return EvalAdd(args);
            else if (op_name == "-") return EvalSub(args);
            else if (op_name == "*") return EvalMul(args);
            else if (op_name == "/") return EvalDiv(args);
            else if (op_name == "=") return EvalEqu(args);
            else if (op_name == "<") return EvalLess(args);
            else if (op_name == ">") return EvalGreater(args);
            else if (op_name == ">=") return EvalGreaterEqual(args);
            else if (op_name == "<=") return EvalLessEqual(args);
            else if (op_name == "cons") return EvalCons(args);
            else if (op_name == "car") return EvalCar(args);
            else if (op_name == "cdr") return EvalCdr(args);
            else if (op_name == "list") return args;
            else if (op_name == "pair?") return EvalPair(args);
            else if (op_name == "null?") return EvalNull(args);
            else if (op_name == "integer?") return EvalInteger(args);
            else if (op_name == "real?") return EvalReal(args);
            else if (op_name == "number?") return EvalNumber(args);
            else if (op_name == "symbol?") return EvalSymbol(args);
            else if (op_name == "string?") return EvalString(args);
            else if (op_name == "boolean?") return EvalBoolean(args);
            else if (op_name == "atom?") return EvalAtom(args);
            else if (op_name == "eqv?") return EvalEqv(args);
            else if (op_name == "equal?") return EvalEqual(args);
            else if (op_name == "not") return EvalNot(args);
            else if (op_name == "string-append") return EvalStringAppend(args);
            else if (op_name == "string>?") return EvalStringGreater(args);
            else if (op_name == "string<?") return EvalStringLess(args);
            else if (op_name == "string=?") return EvalStringEqual(args);
        } else {
            throw EvalError(apply_non_function, "", args);
        }
        return nullptr;
    }

    // 處理 (clean-environment)：清空自定義變數，恢復初始環境
    Node* HandleCleanEnvironment(Node* exp) {
        if (exp != root) {
            throw EvalError(level_of_clean_environment);
        }
        if (ListLength(exp) != 1) {
            throw EvalError(incorrect_num_of_args, "clean-environment");
        }
        cout << "environment cleaned\n";
        curr_env.vars.clear();
        for (string p : prims) {
            curr_env.Define(p, CreatePrimitiveNode(p));
        }
        return nullptr;
    }

    // 處理 (define var val)：將變數綁定存入環境中
    Node* HandleDefine(Node* exp) {
        if (exp != root) {
            throw EvalError(level_of_define);
            return nullptr;
        }

        if (ListLength(exp) != 3) {
            throw EvalError(define_format, "", exp);
            return nullptr;
        }

        Node* args = exp->right;
        if (args == nullptr || args->is_atom) { 
            throw EvalError(define_format, "", exp);
            return nullptr;
        }

        Node* var_node = args->left;
        if (var_node == nullptr || !var_node->is_atom || var_node->token.type != Symbol) { 
            throw EvalError(define_format, "", exp);
            return nullptr;
        }

        Node* val_list = args->right;
        if (val_list == nullptr || val_list->is_atom) {
            throw EvalError(define_format, "", exp);
            return nullptr;
        }

        Node* val_node = val_list->left;
        // 1. 計算 val_node 的實際值
        Node* evaluated_val = Eval(val_node);
        
        // 若 evaluated_val 是來自環境變數 (例如 define b a)，為了讓 eqv? 能判斷為 #t 必須共用指標，因此不 Clone
        // 其他情況必須 CloneTree，避免與當前語法樹一同被 FreeTree 釋放
        if (!curr_env.LookupNode(evaluated_val)) {
            evaluated_val = CloneTree(evaluated_val);
        }

        // 2. 存入環境變數
        string var_name = get<string>(var_node->token.value);
        bool is_special = false;
        for (auto& i: special_forms) {
            if (i == var_name) {
                is_special = true;
                break;
            }
        }

        if (is_special) {
            throw EvalError(define_format, "", exp);
            return nullptr;
        }

        if (curr_env.LookupVar(var_name) != nullptr) {
            Node* old_val = curr_env.LookupVar(var_name);
            if (old_val->token.type == Primitive) {
                throw EvalError(define_format, "", exp);
                return nullptr;
            }
            bool found = false;
            for (auto& x: curr_env.vars) {
                if (x.first != var_name && x.second == old_val) {
                    found = true;
                    break;
                }
                
            }
            if (!found) FreeTree(old_val); // 清理舊值避免 Memory Leak
        }
        curr_env.Define(var_name, evaluated_val);
        define_node = evaluated_val;
        
        cout << var_name << " defined\n";
        return nullptr;
    }

    // 處理 (quote exp) 或是 'exp：直接回傳參數本身不作求值
    Node* HandleQuote(Node* exp) {
        return exp->right->left; 
    }

    // 處理 (if test true-expr [false-expr])
    Node* HandleIf(Node* exp) {
        int len = ListLength(exp);
        if (len != 3 && len != 4) {
            throw EvalError(incorrect_num_of_args, "if");
            return nullptr;
        }
        
        Node* args = exp->right;
        if (args == nullptr || args->is_atom) { 
            throw EvalError(incorrect_num_of_args, "if");
            return nullptr;
        }

        Node* cond_node = args->left;
        if (cond_node == nullptr) { 
            throw EvalError(incorrect_num_of_args, "if");
            return nullptr;
        }

        Node* val_list = args->right;
        if (val_list == nullptr || val_list->is_atom) {
            throw EvalError(incorrect_num_of_args, "if");
            return nullptr;
        }

        Node* val_first = val_list->left;
        if (val_first == nullptr) {
            throw EvalError(incorrect_num_of_args, "if");
            return nullptr;
        }

        Node* val_second = nullptr;
        if (len == 4) {
            val_second = val_list->right->left;
        }

        Node* evaluated_cond = Eval(cond_node);
        if (evaluated_cond != nullptr && evaluated_cond->is_atom && evaluated_cond->token.type == Nil) {
            if (len == 4) {
                return Eval(val_second);
            } else {
                throw EvalError(no_return_value, "", exp);
            }
        }
        return Eval(val_first);
    }

    // 處理 (cond (test1 expr1) (test2 expr2) ... (else exprN))
    Node* HandleCond(Node* exp) {
        if (ListLength(exp) <= 1) {
            throw EvalError(cond_format, "", exp);
            return nullptr;
        }
        Node* args = exp->right;

        while (args != nullptr && args->token.type != Nil) {
            Node* clause = args->left;
            if (clause == nullptr || clause->is_atom) {
                throw EvalError(cond_format, "", exp);
            }
            
            Node* condition = clause->left; // 取得條件
            Node* exprs = clause->right;
            if (exprs == nullptr || (exprs->is_atom && exprs->token.type == Nil)) {
                throw EvalError(cond_format, "", exp);
                return nullptr;
            }
            args = args->right;
        }

        args = exp->right;
        while (args != nullptr && args->token.type != Nil) {
            Node* clause = args->left;
            if (clause == nullptr || clause->is_atom) {
                throw EvalError(cond_format, "", exp);
            }
            
            Node* condition = clause->left; // 取得條件
            bool is_last = (args->right == nullptr || args->right->is_atom && args->right->token.type == Nil);
            bool is_else = (condition->is_atom && condition->token.type == Symbol && get<string>(condition->token.value) == "else" && is_last);
            
            Node* eval_cond = nullptr;
            if (!is_else) {
                eval_cond = Eval(condition); // 評估條件
            }

            // Scheme 中只要條件不為 #f (Nil)，就視為成立
            if (is_else || (eval_cond != nullptr && !(eval_cond->is_atom && eval_cond->token.type == Nil))) {
                Node* exprs = clause->right;
                if (exprs == nullptr || (exprs->is_atom && exprs->token.type == Nil)) {
                    throw EvalError(cond_format, "", exp);
                    return nullptr;
                }
                
                Node* result = nullptr;
                // 循序執行分支內的所有語句，回傳最後一個結果
                while (exprs != nullptr && exprs->token.type != Nil) {
                    result = Eval(exprs->left);
                    exprs = exprs->right;
                }
                return result;
            }
            args = args->right;
        }
        throw EvalError(no_return_value, "", exp);
        return nullptr;
    }
    
    // 處理 (begin exp1 exp2 ...)：循序求值，回傳最後一個結果
    Node* HandleBegin(Node* exp) {
        if (ListLength(exp) <= 1) {
            throw EvalError(incorrect_num_of_args, "begin");
            return nullptr;
        }
        Node* args = exp->right;
        Node* result = EvalList(args);
        return GetLastList(result);
    }

    // 處理 (and exp1 exp2 ...)：短路求值，遇到 #f 則提早結束
    Node* HandleAnd(Node* exp) {
        Node* args = exp->right;
        // Scheme 中，不帶參數的 (and) 應回傳 #t
        Node* result = CreateTrueNode(); 
        while (args != nullptr && args->token.type != Nil) {
            Node* clause = args->left;
            if (clause == nullptr) {
                //throw EvalError(cond_format, "", exp);
                return nullptr;
            }
            result = Eval(clause);
            // 如果評估結果是 #f (Nil)，則提早結束並回傳 #f (短路求值)
            if (result != nullptr && result->is_atom && result->token.type == Nil) {
                return CreateNilNode();
            }
            args = args->right;
        }
        return result;
    }

    // 處理 (or exp1 exp2 ...)：短路求值，遇到非 #f 則提早結束回傳該值
    Node* HandleOr(Node* exp) {
        Node* args = exp->right;
        Node* result = EvalList(args);
        Node* curr = result;
        while (curr != nullptr && curr->token.type != Nil) {
            if (curr->left->is_atom && curr->left->token.type == Nil) {
                curr = curr->right;
                continue;
            }
            return curr->left;
        }
        return CreateNilNode();
    }

    // 處理 (exit)：結束直譯器
    Node* HandleExit(Node* exp) {
        if (exp != root) {
            throw EvalError(level_of_exit);
            return nullptr;
        }
        if (ListLength(exp) != 1) {
            throw EvalError(incorrect_num_of_args, "exit");
            return nullptr;
        }
        return nullptr;
    }

    Node* HandleLambda(Node* exp) {
        Node* args = exp->right;
        if (args == nullptr || args->is_atom) {
            throw EvalError(lambda_format, "", exp);
            return nullptr;
        }
        Node* params = args->left;
        if (params == nullptr || params->is_atom) {
            throw EvalError(lambda_format, "", exp);
            return nullptr;
        }
        Node* body = args->right;
        if (body == nullptr || body->is_atom) {
            throw EvalError(lambda_format, "", exp);
            return nullptr;
        }
        
        Evaluator lam = Evaluator();
    }


public:
    Node* root;         // 當前正在求值的根節點 (用於檢查 define/exit 的層級)
    Node* define_node;  // 紀錄 define 綁定的節點 (避免被 FreeTree 釋放)

    // 建構子：初始化全域環境與 Special Form 名稱
    Evaluator() {
        special_forms[0] = "clean-environment";
        special_forms[1] = "define";
        special_forms[2] = "quote";
        special_forms[3] = "if";
        special_forms[4] = "cond";
        special_forms[5] = "begin";
        special_forms[6] = "and";
        special_forms[7] = "or";
        
        for (string p : prims) {
            curr_env.Define(p, CreatePrimitiveNode(p));
        }
    }
    
    // 遞迴求值核心：接收一個 AST 節點並回傳其求值結果
    Node* Eval(Node* node) {
        if (node == nullptr) return nullptr;

        if (node->is_atom) { //處理 Atom
            Token t = node->token;
            if (t.type == Symbol) {
                string name = get<string>(t.value);
                Node* val = curr_env.LookupVar(name);
                if (val != nullptr) {
                    return val; // 變數查詢
                } else {
                    throw EvalError(unbound_symbol, name);
                }
            }
            return node; 
        } else { //處理 Pair
            // 任何被當作程式碼評估的 Pair 都必須是正規的 List (以 nil 結尾)，否則拋出 non_list 錯誤
            if (ListLength(node) == -1) {
                throw EvalError(non_list, "", node);
            }

            // 先看看這個 Pair 的第一個元素 (left / car) 是什麼
            Node* first_element = node->left;
            
            // 情況 A：檢查是不是 Special Form (例如 define)
            if (first_element->is_atom && first_element->token.type == Symbol) {
                string op = get<string>(first_element->token.value);
                if (op == "clean-environment") return HandleCleanEnvironment(node);
                if (op == "define") return HandleDefine(node);
                if (op == "quote")  return HandleQuote(node);
                if (op == "if")     return HandleIf(node);
                if (op == "cond")   return HandleCond(node);
                if (op == "begin") return HandleBegin(node);
                if (op == "and")   return HandleAnd(node);
                if (op == "or")   return HandleOr(node);
                if (op == "exit") return HandleExit(node);
                if (op == "lambda") return HandleLambda(node);
            }

            // 情況 B：這是一般函式呼叫 (例如 +, -, *, car, cons)
            // 1. 先遞迴求出真正的操作符 (例如把 '+' 這個 Symbol 解析成真正的加法函式指標)
            Node* evaluated_op = Eval(first_element); 
            
            // 提早檢查是否為有效函式，以及參數數量，讓這些錯誤先於參數評估 (unbound symbol) 被發現
            if (evaluated_op == nullptr || !(evaluated_op->is_atom && evaluated_op->token.type == Primitive)) {
                throw EvalError(apply_non_function, "", evaluated_op);
            }

            string op_name = get<string>(evaluated_op->token.value);
            int arg_count = ListLength(node->right);
            
            if (op_name == "not" || op_name == "car" || op_name == "cdr" || 
                op_name == "pair?" || op_name == "null?" || op_name == "integer?" || 
                op_name == "real?" || op_name == "number?" || op_name == "symbol?" || 
                op_name == "string?" || op_name == "boolean?" || op_name == "atom?") {
                if (arg_count != 1) throw EvalError(incorrect_num_of_args, op_name);
            } else if (op_name == "cons" || op_name == "eqv?" || op_name == "equal?") {
                if (arg_count != 2) throw EvalError(incorrect_num_of_args, op_name);
            } else if (op_name == "+" || op_name == "-" || op_name == "*" || op_name == "/" || 
                op_name == "=" || op_name == "<" || op_name == ">" || op_name == "<=" || op_name == ">=" || 
                op_name == "string-append" || op_name == "string>?" || op_name == "string<?" || op_name == "string=?") {
                if (arg_count < 2) throw EvalError(incorrect_num_of_args, op_name);
            }

            // 2. 遞迴求出所有參數的值
            Node* evaluated_args = EvalList(node->right); // EvalList 是一個輔助函式，它會走訪串列，對每一個 Node 呼叫 Eval()
            
            // 3. 把算好的參數交給操作符去執行 (這個步驟在 Lisp 中稱為 Apply)
            return Apply(evaluated_op, evaluated_args); 
        }
    }
};

// 印出空白的輔助函式
void PrintSpace(int num) {
    for (int i = 0; i < num; i++) cout << ' ';
}

// Pretty Print 列印 S-exp
void PrintSExp(Node* node, int M) {
    if (node == nullptr) return;

    if (node->is_atom) {
        Token t = node->token;
        if (t.type == Int) cout << get<int>(t.value) << "\n";
        else if (t.type == Float) printf("%.3f\n", get<float>(t.value));
        else if (t.type == Nil) cout << "nil\n";
        else if (t.type == T) cout << "#t\n";
        else if (t.type == Primitive) cout << "#<procedure " << get<string>(t.value) << ">\n";
        else cout << get<string>(t.value) << "\n"; // Symbol 或 String
    } else {
        // 這是一個 Pair (括號結構)
        cout << "( ";
        PrintSExp(node->left, M + 2);

        Node* curr = node->right;
        while (curr != nullptr && !curr->is_atom) {
            PrintSpace(M + 2);
            PrintSExp(curr->left, M + 2);
            curr = curr->right;
        }

        if (curr != nullptr && !(curr->is_atom && curr->token.type == Nil)) {
            // 如果右結尾不是 nil，表示有 Dotted pair
            PrintSpace(M + 2);
            cout << ".\n";
            PrintSpace(M + 2);
            PrintSExp(curr, M + 2);
        }

        PrintSpace(M);
        cout << ")\n";
    }
}

// 檢查是否為 (exit) 指令
bool IsExit(Node* root) {
    if (!root) return false;
    if (!root->is_atom) {
        if (root->left && root->left->is_atom && root->left->token.type == Symbol && get<string>(root->left->token.value) == "exit") {
            if (root->right && root->right->is_atom && root->right->token.type == Nil) {
                return true;
            }
        }
    }
    return false;
}


int main() {
    string whatever;
    getline(cin, whatever);
    Parser parser;
    Evaluator evaluator;
    cout << "Welcome to OurScheme!\n";
    while (true) {
        cout << "\n> ";
        Node* root = nullptr;
        try {
            parser.ReadyForNewSExp();
            root = parser.ReadSExp();
            
            // 處理 (exit)
            if (IsExit(root)) {
                FreeTree(root);
                cout << "\nThanks for using OurScheme!\n";
                break;
            }

            try {
                evaluator.root = root;
                Node* eval_result = evaluator.Eval(root);

                // 列印樹狀結構
                PrintSExp(eval_result, 0);
                FreeTree(root, evaluator.define_node); 
            } catch (EvalError& e) {
                // 捕捉各種求值錯誤並印出相應訊息
                if (e.type == define_format) {
                    cout << "ERROR (DEFINE format) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == incorrect_num_of_args) {
                    cout << "ERROR (incorrect number of arguments) : " << e.msg << "\n";
                } else if (e.type == unbound_symbol) {
                    cout << "ERROR (unbound symbol) : " << e.msg << "\n";
                } else if (e.type == level_of_clean_environment) {
                    cout << "ERROR (level of CLEAN-ENVIRONMENT)\n";
                } else if (e.type == apply_non_function) {
                    cout << "ERROR (attempt to apply non-function) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == level_of_define) {
                    cout << "ERROR (level of DEFINE)\n";
                } else if (e.type == incorrect_arg_type) {
                    cout << "ERROR (" << e.msg <<  " with incorrect argument type) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == non_list) {
                    cout << "ERROR (non-list) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == division_by_zero) {
                    cout << "ERROR (division by zero) : /\n";
                } else if (e.type == level_of_exit) {
                    cout << "ERROR (level of EXIT)\n";
                } else if (e.type == cond_format) {
                    cout << "ERROR (COND format) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == no_return_value) {
                    cout << "ERROR (no return value) : ";
                    PrintSExp(e.err_node, 0);
                } else if (e.type == lambda_format) {
                    cout << "ERROR (LAMBDA format) : ";
                    PrintSExp(e.err_node, 0);
                }
            }
        } catch (ParseError& e) {
            // 捕捉各種剖析錯誤並印出相應訊息
            if (e.type == no_more_input) {
                cout << "ERROR (no more input) : END-OF-FILE encountered\n";
                cout << "Thanks for using OurScheme!\n";
                FreeTree(root);
                break;
            } else if (e.type == no_closing_quote) {
                cout << "ERROR (no closing quote) : END-OF-LINE encountered at Line " << e.line << " Column " << e.col << "\n";
                parser.DiscardLine();
            } else if (e.type == unexpected_token_atom) {
                cout << "ERROR (unexpected token) : atom or '(' expected when token at Line " << e.line << " Column " << e.col << " is >>" << e.token_str << "<<\n";
                parser.DiscardLine();
            } else if (e.type == unexpected_token_paren) {
                cout << "ERROR (unexpected token) : ')' expected when token at Line " << e.line << " Column " << e.col << " is >>" << e.token_str << "<<\n";
                parser.DiscardLine();
            }
            FreeTree(root);
        }
    }
    return 0;
}