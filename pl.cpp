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
    Symbol, Int, Float, String, Nil, T,
    LeftParen, RightParen, Dot, Quote,
    EndOfFile, ErrorToken
};

// 定義錯誤的種類
enum Error_Type {
    no_closing_quote,
    no_more_input,
    unexpected_token_atom,  // 預期要 Atom 或 '('
    unexpected_token_paren,  // 預期要 ')'
};

enum EvalError_Type {
    unbound_symbol,             // 未定義的符號
    non_list,                   // 不是 list (例如 cons 遇到錯誤參數)
    incorrect_num_of_args,      // 參數數量錯誤
    incorrect_arg_type,         // 參數型態錯誤 (例如 car 遇到數字、+ 遇到字串)
    apply_non_function,         // 嘗試把非函式當函式呼叫
    no_return_value,            // if 或 cond 沒有回傳值
    division_by_zero,           // 除以零
    define_format,              // define 格式錯誤
    cond_format                 // cond 格式錯誤
};

// 記錄錯誤的 Exception 結構
struct ParseError : public exception {
    Error_Type type;
    int line;
    int col;
    string token_str;
    ParseError(Error_Type t, int l, int c, string s) : type(t), line(l), col(c), token_str(s){}
};

// Eval 專用的 Exception
struct EvalError : public exception {
    EvalError_Type type;
    string msg;     // 用來存放符號名稱 (例如 "a", "+", "car")
    Node* err_node; // 用來存放需要被 PrintSExp 印出來的語法樹節點

    // 建構子 1：只需要印字串的錯誤 (例如 unbound_symbol, division_by_zero)
    EvalError(EvalError_Type t, string m) : type(t), msg(m), err_node(nullptr) {}

    // 建構子 2：需要印出一整坨 S-exp 的錯誤 (例如 define_format, non_list)
    EvalError(EvalError_Type t, string m, Node* n) : type(t), msg(m), err_node(n) {}
};

// Token 結構
struct Token {
    Token_Type type;
    string original_value;
    int line;
    int col;
    variant<int, float, string> value;

    Token() : type(ErrorToken), line(0), col(0) {}
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

// 刪除整個樹
void FreeTree(Node* node) {
    if (!node) return;
    if (!node->is_atom) {
        FreeTree(node->left);
        FreeTree(node->right);
    }
    delete node;
}

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

    // 處理字串內的跳脫字元
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

    Token GetNext() {
        if (has_peek) {
            has_peek = false;
            return peek_token;
        }
        return scanner.GetNextToken();
    }

    Token PeekNext() {
        if (!has_peek) {
            peek_token = scanner.GetNextToken();
            has_peek = true;
        }
        return peek_token;
    }

public:
    void ReadyForNewSExp() {
        scanner.ReadyForNewSExp();
    }

    void DiscardLine() {
        scanner.DiscardRestOfLine();
        has_peek = false; 
    }

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


class Evaluator {
private:
    map<string, Node*> environment;
    
    Node* GetLastList(Node* list) {
        Node* current = list;
        while (current->right != nullptr && current->right->token.type != Nil) {
            current = current->right;
        }
        return current->left;
    }

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

    Node* Cons(Node* car, Node* cdr) {
        Node* new_node = new Node(car, cdr);
        return new_node;
    }

    Node* CreateIntNode(int val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Int, to_string(val));
        return n;
    }

    Node* CreateFloatNode(float val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Float, to_string(val));
        return n;
    }

    Node* CreateNilNode() {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(Nil, "nil");
        return n;
    }

    Node* CreateTrueNode() {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(T, "#t");
        return n;
    }

    Node* CreateStringNode(string val) {
        Node* n = new Node();
        n->is_atom = true;
        n->token = Token(String, val);
        return n;
    }

    Node* EvalCar(Node* args) {
        if (args == nullptr) return nullptr;
        Node* arg_val = args->left; // 取得第一個參數
        if (arg_val == nullptr || arg_val->is_atom) {
            // 可以在此加入錯誤處理，例如 throw EvalError("...");
            return nullptr; 
        }
        return arg_val->left; // 回傳該參數的 car
    }

    Node* EvalCdr(Node* args) {
        if (args == nullptr) return nullptr;
        Node* arg_val = args->left; // 取得第一個參數
        if (arg_val == nullptr || arg_val->is_atom) {
            // 可以在此加入錯誤處理
            return nullptr; 
        }
        return arg_val->right; // 回傳該參數的 cdr
    }

    Node* EvalAdd(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                //throw EvalError("ERROR (+ with incorrect argument type)");
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

    Node* EvalSub(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                //throw EvalError("ERROR (- with incorrect argument type)");
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
                //throw EvalError("ERROR (- with incorrect argument type)");
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

    Node* EvalMul(Node* args) {
        bool is_float = false;
        float f_sum = 1.0f; // 乘法初始值修正為 1
        int i_sum = 1;
        Node* current = args;
        
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                //throw EvalError("ERROR (* with incorrect argument type)");
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

    Node* EvalDiv(Node* args) {
        bool is_float = false;
        float f_sum = 0.0f;
        int i_sum = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != Int && arg_val->token.type != Float)) { 
                //throw EvalError("ERROR (/ with incorrect argument type)");
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
                //throw EvalError("ERROR (/ with incorrect argument type)");
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
                    }
                }
            }
            current = current->right; // 走到下一個算好的參數
        }
        
        if (is_float) return CreateFloatNode(f_sum);
        return CreateIntNode(i_sum);
    }

    Node* EvalEqu(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
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

    Node* EvalLess(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
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

    Node* EvalGreater(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
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

    Node* EvalGreaterEqual(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
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

    Node* EvalLessEqual(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        bool prev_is_float = false;
        float f_prev = 0.0f;
        int i_prev = 0;
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
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

    Node* EvalCons(Node* args) {
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
    
    Node* EvalList(Node* args) {
        if (args == nullptr || args->token.type == Nil) {
            return args; // 到底了，回傳 Nil
        }
        Node* evaluated_car = Eval(args->left);       // 算左邊的單一參數
        Node* evaluated_cdr = EvalList(args->right);  // 遞迴處理剩下的串列
        return Cons(evaluated_car, evaluated_cdr);    // 重新組裝回傳
    }

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

    Node* EvalNot(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateNilNode();

        Node* target = args->left;

        if (target != nullptr && target->is_atom && target->token.type == Nil) {
            return CreateTrueNode();
        }
        return CreateNilNode();
    }
    
    Node* EvalStringAppend(Node* args) {
        Node* current = args;
        string str = "";
        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
            
            if (arg_val == nullptr || (arg_val->token.type != String)) { 
                //throw EvalError("ERROR (+ with incorrect argument type)");
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

    Node* EvalStringGreater(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
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

    Node* EvalStringLess(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
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

    Node* EvalStringEqual(Node* args) {
        if (args == nullptr || args->token.type == Nil) return CreateTrueNode();

        string str_prev = "";
        Node* current = args;
        
        if (current != nullptr && current->token.type != Nil) { 
            Node* arg_val = current->left; 
            if (arg_val->token.type == String) {
                str_prev = get<string>(arg_val->token.value);
            }
            current = current->right;
        }

        while (current != nullptr && current->token.type != Nil) {
            Node* arg_val = current->left; 
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

    Node* Apply(Node* op, Node* args) {
        if (op->is_atom && op->token.type == Symbol) {
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
            else if (op_name == "eqv?") return EvalEqv(args);
            else if (op_name == "equal?") return EvalEqual(args);
            else if (op_name == "not") return EvalNot(args);
            else if (op_name == "string-append") return EvalStringAppend(args);
            else if (op_name == "string>?") return EvalStringGreater(args);
            else if (op_name == "string<?") return EvalStringLess(args);
            else if (op_name == "string=?") return EvalStringEqual(args);
        }
        return nullptr;
    }

    Node* HandleDefine(Node* exp) {
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
        bool from_env = false;
        for (auto& x : environment) {
            if (x.second == evaluated_val) {
                from_env = true;
                break;
            }
        }
        // 其他情況必須 CloneTree，避免與當前語法樹一同被 FreeTree 釋放
        if (!from_env) {
            evaluated_val = CloneTree(evaluated_val);
        }

        // 2. 存入環境變數
        string var_name = get<string>(var_node->token.value);
        if (environment.count(var_name)) {
            Node* old_val = environment[var_name];
            bool found = false;
            for (auto& x: environment) {
                if (x.first != var_name && x.second == old_val) {
                    found = true;
                    break;
                }
                
            }
            if (!found) FreeTree(old_val); // 清理舊值避免 Memory Leak
        }
        environment[var_name] = evaluated_val;
        define_node = evaluated_val;
        
        cout << var_name << " defined\n";
        return nullptr;
    }

    Node* HandleQuote(Node* exp) {
        return exp->right->left; 
    }

    Node* HandleIf(Node* exp) {
        Node* args = exp->right;
        if (args == nullptr || args->is_atom) { 
            //throw ParseError(define_format, 0, 0, "0");
            return nullptr;
        }

        Node* cond_node = args->left;
        if (cond_node == nullptr) { 
            //throw ParseError(define_format, 0, 0, "0");
            return nullptr;
        }

        Node* val_list = args->right;
        if (val_list == nullptr || val_list->is_atom) {
            //throw ParseError(define_format, 0, 0, "0");
            return nullptr;
        }

        Node* val_first = val_list->left;
        if (val_first == nullptr) {
            //throw ParseError(define_format, 0, 0, "0");
            return nullptr;
        }

        Node* val_second = val_list->right->left;

        Node* evaluated_cond = Eval(cond_node);
        if (evaluated_cond != nullptr && evaluated_cond->is_atom && evaluated_cond->token.type == Nil) {
            return Eval(val_second);
        }
        return Eval(val_first);
    }

    Node* HandleCond(Node* exp) {
        Node* args = exp->right;
        while (args != nullptr && args->token.type != Nil) {
            Node* clause = args->left;
            if (clause == nullptr || clause->is_atom) {
                return nullptr; // 分支格式不正確
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
                    // 分支內只有條件沒有執行語句，回傳條件的評估結果
                    return is_else ? CreateNilNode() : eval_cond;
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
        return CreateNilNode(); // 若全部條件都不成立，預設回傳 nil
    }
    
    Node* HandleBegin(Node* exp) {
        Node* args = exp->right;
        Node* result = EvalList(args);
        return GetLastList(result);
    }

    Node* HandleAnd(Node* exp) {
        Node* args = exp->right;
        Node* result = EvalList(args);
        Node* curr = result;
        while (curr != nullptr && curr->token.type != Nil) {
            if (curr->left->is_atom && curr->left->token.type == Nil) {
                return CreateNilNode();
            }
            curr = curr->right;
        }
        return GetLastList(result);
    }

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

public:
    Node* define_node;

    Node* Eval(Node* node) {
        if (node == nullptr) return nullptr;

        if (node->is_atom) { //處理 Atom
            Token t = node->token;
            if (t.type == Symbol) {
                string name = get<string>(t.value);
                if (environment.count(name)) {
                    return environment[name]; // 變數查詢
                } else {
                    //throw EvalError("ERROR (unbound symbol): " + name); //[cite: 32, 44-45]
                }
            }
            return node; 
        } else { //處理 Pair
            // 先看看這個 Pair 的第一個元素 (left / car) 是什麼
            Node* first_element = node->left;
            
            // 情況 A：檢查是不是 Special Form (例如 define)
            if (first_element->is_atom && first_element->token.type == Symbol) {
                string op = get<string>(first_element->token.value);
                if (op == "define") return HandleDefine(node);
                if (op == "quote")  return HandleQuote(node);
                if (op == "if")     return HandleIf(node);
                if (op == "cond")   return HandleCond(node);
                if (op == "begin") return HandleBegin(node);
                if (op == "and")   return HandleAnd(node);
                if (op == "or")   return HandleOr(node);
            }

            // 情況 B：這是一般函式呼叫 (例如 +, -, *, car, cons)
            // 1. 先遞迴求出真正的操作符 (例如把 '+' 這個 Symbol 解析成真正的加法函式指標)
            Node* evaluated_op = Eval(first_element); 
            
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

// 檢查是否為 (clean-environment) 指令
bool IsClearEnvironment(Node* root) {
    if (!root) return false;
    if (!root->is_atom) {
        if (root->left && root->left->is_atom && root->left->token.type == Symbol && get<string>(root->left->token.value) == "clean-environment") {
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

            // 處理 (clean-environment)
            if (IsClearEnvironment(root)) {
                cout << "environment cleaned\n";
                FreeTree(root);
                continue;
            }
            try {
            Node* eval_result = evaluator.Eval(root);

            // 列印樹狀結構
            PrintSExp(eval_result, 0);
            FreeTree(root, evaluator.define_node); 
            } catch (EvalError& e) {
                // 捕捉各種求值錯誤並印出相應訊息
                if (e.type == define_format) {
                cout << "ERROR (DEFINE format) : ";
                PrintSExp(e.err_node, 0);
                parser.DiscardLine();
                } else if (e.type == unbound_symbol) {
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