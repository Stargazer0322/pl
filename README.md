# OurScheme Interpreter

這是一個使用 C++ 實作的簡易 Scheme 解譯器。程式會讀取 S-expression，
經過 tokenizer、parser 與 evaluator 處理後，在終端機輸出執行結果。

## 功能

- 整數、浮點數、字串、布林值與 symbol
- list、pair 與 dotted pair
- quote 語法（`'`）
- 變數定義與環境管理
- 算術與比較運算
- `if`、`cond`、`lambda` 與 `let`
- lexical scoping 與 closure
- 參數數量、型別、未綁定 symbol、除以零等錯誤訊息
- `verbose` 模式與 `error-object?` 等輔助功能

## 專案檔案

| 檔案 | 說明 |
| --- | --- |
| `pl.cpp` | 解譯器的主要原始碼 |
| `test.scm` | Scheme 測試範例 |
| `pl.exe` | Windows 上已編譯的執行檔（若檔案存在） |
| `OurScheme*.pdf` | OurScheme 規格與專案說明文件 |

## 編譯

需要支援 C++17 的編譯器，例如 MinGW-w64 的 `g++`。

### Windows

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -o pl.exe pl.cpp
```

### Linux / macOS

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o pl pl.cpp
```

`std::variant` 是 C++17 的功能，因此不能使用 C++11 或更早的標準編譯。

## 執行

Windows：

```powershell
.\pl.exe
```

Linux / macOS：

```bash
./pl
```

啟動後會進入互動式提示字元 `>`。輸入一個 Scheme expression 後按下
Enter，即可查看結果。輸入 `exit` 結束程式。

```scheme
> (+ 1 2)
3

> (define x 10)

> (* x 3)
30

> (if (> x 5) "large" "small")
"large"

> ((lambda (x) (+ x 1)) 41)
42

> (let ((x 2) (y 3)) (+ x y))
5

> exit
Thanks for using OurScheme!
```

字串、list 與 quote 的例子：

```scheme
> (list 1 2 "three")
(1 2 "three")

> '(a b c)
(a b c)

> (cons 1 '(2 3))
(1 2 3)
```

## 內建函式

### 算術與比較

`+`、`-`、`*`、`/`、`=`、`<`、`>`、`<=`、`>=`

### List 與 pair

`cons`、`car`、`cdr`、`list`、`list?`、`pair?`、`null?`

### 型別判斷

`integer?`、`real?`、`number?`、`symbol?`、`string?`、`boolean?`、
`atom?`

### 相等、布林與字串

`eqv?`、`equal?`、`not`、`string-append`、`string>?`、`string<?`、
`string=?`

### 其他

`define`、`if`、`cond`、`lambda`、`let`、`verbose`、`verbose?`、
`error-object?`、`clean-environment`、`exit`

## 錯誤處理

程式會將 parser 與 evaluator 錯誤輸出到終端機，例如：

- `ERROR (unbound symbol)`：使用尚未定義的 symbol
- `ERROR (incorrect number of arguments)`：函式參數數量不正確
- `ERROR (... with incorrect argument type)`：函式參數型別不正確
- `ERROR (division by zero)`：除數為零
- `ERROR (unexpected token)`：輸入的 S-expression 語法錯誤

發生可恢復的輸入錯誤時，程式會清除目前 expression 並繼續等待下一個
expression。

## 測試

可以參考 `test.scm` 中的 Scheme expression，將內容逐段貼到互動式
解譯器中執行。若要建立自動化測試，建議確認每個 expression 的輸出與
錯誤訊息是否符合 OurScheme 規格。

## 限制

- 目前專案是一個單一 C++ 原始碼檔，沒有獨立的測試框架或建置系統。
- 解譯器以終端機互動輸入為主要使用方式。
- 實際支援的語法與錯誤訊息以 `pl.cpp` 和專案規格文件為準。

## 授權

目前 repository 沒有附上獨立的 LICENSE 檔案；如需公開或重製此專案，
請先確認原始碼與相關課程文件的授權條款。
