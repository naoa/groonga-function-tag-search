# tag_search Groonga function

* ``tag_search(index, "query_1"[,.., "query_n", {op: "OR", mode: "EXACT or PREFIX", threshold: 1000])``

いい感じにタグ検索を行うためのセレクタ関数。
thresholdでシーケンシャルサーチを行うか行わないか調整できる(予定)。

query: 複数指定可能。queryの末尾が``*``で終わる場合は、そのクエリのみprefixサーチとなる。  
op: queryを複数指定した場合の論理演算子AND|OR|NOT。デフォルトOR。ORの場合、in_values相当(いずれかを含む)になる。  
mode: 全体のクエリに設定するモード EXACT: 完全一致 PREFIX: 前方一致 デフォルトEXACT   
threshold: シーケンシャルサーチを行う閾値。threshold以下の場合にのみシーケンシャルサーチになる(予定。本体パッチで運用しているため、いまのところ未実装)。  

## Install

Install libgroonga-dev.

Build this function.

    % sh autogen.sh
    % ./configure
    % make
    % sudo make install

## Usage

Register `functions/tag_search`:

    % groonga DB
    > register functions/tag_search

## Author

Naoya Murakami naoya@createfield.com

## License

LGPL 2.1. See COPYING-LGPL-2.1 for details.
