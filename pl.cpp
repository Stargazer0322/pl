#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <variant>
#include <stdexcept>
#include <cstdio>

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
    unexpected_token_paren  // 預期要 ')'
};

// 記錄錯誤的 Exception 結構
struct ParseError : public exception {
    Error_Type type;
    int line;
    int col;
    string token_str;
    ParseError(Error_Type t, int l, int c, string s) : type(t), line(l), col(c), token_str(s) {}
};

// Token 結構
struct Token {
    Token_Type type;
    string str_value;
    int line;
    int col;
    variant<int, float, string> value;

    Token() : type(ErrorToken), line(0), col(0) {}
    Token(Token_Type t, string s, int l, int c) : type(t), str_value(s), line(l), col(c) {
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

// =========================================================================
// Scanner 類別 (Lexer)
// 負責讀取字元並切分成 Token 串流
// =========================================================================
class Scanner {
private:
    string line_str;
    int line = 0;
    int col = 1;
    int pos = 0;
    bool eof_reached = false;

    // 讀取新的一行
    void LoadNextLine() {
        if (!getline(cin, line_str)) {
            eof_reached = true;
            return;
        }
        line_str += '\n'; // 補回換行字元，方便錯誤判定與字串處理
        line++;
        col = 1;
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

    // 處理字串內的跳脫字元 (將 \n, \t 轉換為實際的控制字元)
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
    Scanner() { LoadNextLine(); }

    // 發生錯誤時，將游標移到行尾，達到「整行忽略」的效果
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

            // 1. 跳過空白字元
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                pos++; col++;
                continue;
            }

            // 2. 跳過註解 (直接忽略到行尾)
            if (c == ';') {
                pos = line_str.length();
                continue;
            }

            int start_col = col;

            // 3. 處理單一字元 Token
            if (c == '(') { pos++; col++; return Token(LeftParen, "(", line, start_col); }
            if (c == ')') { pos++; col++; return Token(RightParen, ")", line, start_col); }
            if (c == '\'') { pos++; col++; return Token(Quote, "'", line, start_col); }

            // 4. 處理字串 (String)
            if (c == '"') {
                string raw = "\"";
                pos++; col++;
                bool closed = false;
                while (pos < line_str.length()) {
                    char sc = line_str[pos];
                    if (sc == '\n' || sc == '\r') {
                        // 遇到換行仍未閉合，丟出錯誤
                        throw ParseError(no_closing_quote, line, col, "");
                    }
                    if (sc == '\\') {
                        raw += sc; pos++; col++;
                        if (pos < line_str.length() && line_str[pos] != '\n' && line_str[pos] != '\r') {
                            raw += line_str[pos]; pos++; col++;
                        }
                        continue;
                    }
                    if (sc == '"') {
                        raw += '"'; pos++; col++;
                        closed = true;
                        break;
                    }
                    raw += sc; pos++; col++;
                }
                if (!closed) throw ParseError(no_closing_quote, line, col, "");
                return Token(String, ProcessString(raw), line, start_col);
            }

            // 5. 處理連續字元 (讀取到分隔符號為止)
            string seq = "";
            while (pos < line_str.length()) {
                char sc = line_str[pos];
                if (sc == ' ' || sc == '\t' || sc == '\n' || sc == '\r' ||
                    sc == '(' || sc == ')' || sc == '\'' || sc == '"' || sc == ';') {
                    break;
                }
                seq += sc;
                pos++; col++;
            }

            // 判斷該連續字元屬於哪一種 Token
            if (seq == ".") return Token(Dot, ".", line, start_col);
            if (seq == "t" || seq == "#t") return Token(T, "#t", line, start_col);
            if (seq == "nil" || seq == "#f" || seq == "()") return Token(Nil, "nil", line, start_col);

            if (IsInt(seq)) return Token(Int, seq, line, start_col);
            if (IsFloat(seq)) return Token(Float, seq, line, start_col);

            return Token(Symbol, seq, line, start_col);
        }
    }
};

// =========================================================================
// Parser 類別 (語法分析器)
// 負責將 Token 串流組合成 S-expression 樹狀結構
// =========================================================================
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
    void DiscardLine() {
        scanner.DiscardRestOfLine();
        has_peek = false; // 偷看的 token 也因為在那一行所以要丟棄
    }

    // 遞迴讀取一個完整的 S-exp
    Node* ReadSExp() {
        Token t = GetNext();
        if (t.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");

        // 句首不能是 ')' 或 '.' 
        if (t.type == RightParen || t.type == Dot) {
            throw ParseError(unexpected_token_atom, t.line, t.col, t.str_value);
        }

        // 如果是 '('，就開始解析 List
        if (t.type == LeftParen) {
            return ReadList();
        }

        // 如果是單引號 '，將其轉換為 (quote S-exp)
        if (t.type == Quote) {
            Node* inner = ReadSExp();
            Node* q = new Node(Token(Symbol, "quote", t.line, t.col));
            Node* pair2 = new Node(inner, new Node(Token(Nil, "nil", t.line, t.col)));
            return new Node(q, pair2);
        }

        // 否則為一般的 Atom
        return new Node(t);
    }

    // 遞迴讀取 List 的內部元素
    Node* ReadList() {
        Token t = PeekNext();
        if (t.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");

        // 遇到 ')' 代表 List 正常結束，回傳 nil
        if (t.type == RightParen) {
            GetNext(); // 消耗掉 ')'
            return new Node(Token(Nil, "nil", t.line, t.col));
        }

        // 遇到 '.' 代表這是一個 Dotted Pair
        if (t.type == Dot) {
            GetNext(); // 消耗掉 '.'
            Node* snn = ReadSExp(); // Dot 後面接一個 S-exp
            Token p = GetNext();    // 然後必須馬上接 ')'
            if (p.type != RightParen) {
                if (p.type == EndOfFile) throw ParseError(no_more_input, -1, -1, "");
                throw ParseError(unexpected_token_paren, p.line, p.col, p.str_value);
            }
            return snn;
        }

        // 繼續讀取目前的節點，然後遞迴讀取剩下的 List
        Node* left = ReadSExp();
        Node* right = ReadList();
        return new Node(left, right);
    }
};

// =========================================================================
// 系統核心與列印功能
// =========================================================================

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
        cout << "(";
        PrintSExp(node->left, M + 1); // 第一個元素不加 M+2 空格

        Node* curr = node->right;
        while (curr != nullptr && !curr->is_atom) {
            PrintSpace(M + 2);
            PrintSExp(curr->left, M + 2);
            curr = curr->right;
        }

        if (curr != nullptr && !(curr->is_atom && curr->token.type == Nil)) {
            // 如果右結尾不是 nil，表示有 Dotted pair
            PrintSpace(M + 2); cout << ".\n";
            PrintSpace(M + 2); PrintSExp(curr, M + 2);
        }

        PrintSpace(M); cout << ")\n";
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
    cout << "Welcome to OurScheme!\n";
    while (true) {
        cout << "\n> ";
        try {
            Node* root = parser.ReadSExp();
            
            // 處理 (exit)
            if (IsExit(root)) {
                FreeTree(root);
                cout << "\nThanks for using OurScheme!\n";
                break;
            }

            // 列印樹狀結構
            PrintSExp(root, 0);
            FreeTree(root); // 釋放記憶體

        } catch (ParseError& e) {
            // 捕捉各種剖析錯誤並印出相應訊息
            if (e.type == no_more_input) {
                cout << "ERROR (no more input) : END-OF-FILE encountered\n";
                cout << "Thanks for using OurScheme!\n";
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
        }
    }
    return 0;
}