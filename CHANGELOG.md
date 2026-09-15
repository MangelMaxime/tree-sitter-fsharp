---
last_commit_released: 77b822c9bf16a9ab28728395bbaff23b72c49ba6
name: tree-sitter-fsharp
updaters:
  - json:
      file: tree-sitter.json
      pointer: /metadata/version
  - package.json:
      file: package.json
  - command: ./build.sh generate --force
---

# Changelog

All notable changes to this project will be documented in this file.

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

This changelog is generated using [EasyBuild.ShipIt](https://github.com/easybuild-org/EasyBuild.ShipIt).

⚠ Only edit the front matter metadata at the top of this file. All other changes will be overwritten when a new release is created.

## 0.2.2 - 2026-09-15

### 🐞 Bug Fixes

* *(queries)* Colour every parameter of a binding or member, not only the first ([77b822c](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/77b822c9bf16a9ab28728395bbaff23b72c49ba6))

<strong><small>[View changes on Github](https://github.com/MangelMaxime/tree-sitter-fsharp/compare/73fcdcd729c98a2f411e7c8271787056baf5e295..77b822c9bf16a9ab28728395bbaff23b72c49ba6)</small></strong>

## 0.2.1 - 2026-09-15

### 🐞 Bug Fixes

* *(ci)* Open the Zed extension pull request on the fork, not its parent ([05d499c](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/05d499cec0d65fa4e7899def14babbe2620c2434))
* *(ci)* Run the Zed extension update with the tool from main ([1035eaf](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/1035eaf5a88ba129aeb0209d6b44558112176960))
* *(queries)* Keep keyword lists in the signature queries and test them like tree-sitter-ocaml ([90afa2b](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/90afa2b9633ab69050c4c114d7e01198b6a3cd8f))

<strong><small>[View changes on Github](https://github.com/MangelMaxime/tree-sitter-fsharp/compare/5dfc2042acebd5a0dcbf7a1b1516b65c87b90898..73fcdcd729c98a2f411e7c8271787056baf5e295)</small></strong>

## 0.2.0 - 2026-09-15

### 🚀 Features

* *(signature)* Add a derived grammar for .fsi files ([2868bc9](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/2868bc902959ebb3dcdda562dc731313364fbe21))

### 🐞 Bug Fixes

* *(build)* Pin FSharp.Core so runners with a newer SDK keep the lock file ([5dfc204](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/5dfc2042acebd5a0dcbf7a1b1516b65c87b90898))
* *(grammar)* Close 23 real-world parse gaps found by the bench triage ([c4205b2](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/c4205b245bf9ed0c16de20eb53ab76e409564474))
* *(grammar)* Keep attributes and module abbreviations attached after a line comment ([29a6489](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/29a6489ce91575ea6d7e4245173702a925d06560))
* *(queries)* Colour bare union cases, optional-parameter markers, type-test aliases and .[ ([6279afd](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/6279afd20651c2e3d3a27d81a8033f410e3e45ac))
* *(scanner)* Keep the scanner state per parser instead of in globals ([c94ddcf](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/c94ddcfe2e293820f863669d168fac48acb2c9ce))

<strong><small>[View changes on Github](https://github.com/MangelMaxime/tree-sitter-fsharp/compare/ebeef78821a64ed105d48cfaf198ffdfd833e92a..5dfc2042acebd5a0dcbf7a1b1516b65c87b90898)</small></strong>

## 0.1.0 - 2026-09-12

### 🚀 Features

* Scm update Neovim from experimental to supported (#4) ([38d7f03](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/38d7f03fdffa5e32ec223b3a935dc07ee36c0f3c))
* *(grammar)* Reserve keywords that can never be an identifier ([a7d1106](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/a7d110633c423cf390ed4f5a9f92558a30c5247e))
* *(grammar)* Reserve 18 more keywords; allow access modifiers on exceptions ([374184d](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/374184db55fad09c740b30138f253e9de5c3dd4d))
* *(grammar)* Support verbose module bodies (begin/end) ([15cf0b5](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/15cf0b50c80260ab1a32f376ce090bdbc811aea3))
* *(grammar)* Reserve type, trading one file for +4.25pp recall ([a0b1733](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/a0b1733a3773dd08cd7b37439363490bd4d2d37d))
* *(grammar)* Support signature files and bodiless member declarations ([870d8e4](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/870d8e4a22bea383d850f6f0a9e5fa2e497fd273))
* *(grammar)* Slices, from-end indices, prefix operands, use patterns, and legacy object members ([7c8db1e](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/7c8db1edff883eea441af7aa89a51aab82ab9ae3))
* *(grammar)* Nullable types in more positions, typar intersections, return-type attributes ([91d4fe6](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/91d4fe62ecdaeed28d2a936f71ae0955a6a66bf3))
* *(grammar)* Tuple binding modifiers, constructor elements, active patterns and operators as parameters ([405edaa](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/405edaa391a5a5d3c1fc5cec15c7147df31fcfd6))
* *(grammar)* Cons, or and and patterns as let names, as binding a pattern, operator application arguments ([addbe00](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/addbe00a256de97692814b763e0185a7fd992238))
* *(grammar)* Members below a same-line type body and the spaced dynamic lookup operator ([754ce74](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/754ce74e535033739ffb2c9664d91e4594e8b2c0))
* *(grammar)* Spaced ? as dynamic lookup, return-type attributes, fixed, literal suffixes (#2) ([b7efe88](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/b7efe889d490237b994a32631e8ec3d6f6f6d41b))

### 🐞 Bug Fixes

* Accept subtype constraints in tupled parameters ([66251c1](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/66251c1bab04c225a0bc186b014b507e80318061))
* Allow a nullable type after a subtype constraint ([83cbe7d](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/83cbe7d5e410571e1ffe1eec75096953a0363143))
* Accept a typed head in an unparenthesized cons pattern ([1f85166](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/1f85166a3bca6cdbaba5e91e658ec7eb2a3c4b79))
* Accept verbatim and triple-quoted strings in patterns ([86824bd](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/86824bdab500f8a9e0cb6d394071aaa48180be68))
* Allow as this on a secondary constructor ([0787df0](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/0787df0fab46f425a50c385828392e59b4985854))
* Parse object construction expressions with inherit ([200b6b4](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/200b6b4ff74c32f2e8fa2fe3c80c76efd9f31e0a))
* Read a string-argument application as a copy-update base ([2bd06fd](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/2bd06fdfdfac8ee577d3025e2c1ba7ef6f6bd2e2))
* Read a parenthesised expression as a copy-update base ([35f9c8b](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/35f9c8b5d8cefdac87d941cd523558f1d56a2ba7))
* *(bench)* Count MISSING nodes, which the gate never saw ([c7d71d8](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/c7d71d85d46d95de08ec03f93340a59018be79d2))
* *(grammar)* Accept bare type ascriptions in four more positions ([a43c691](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/a43c69176be83d238fb6aa22b99524985e1ea93e))
* *(grammar)* Allow an attribute on a parameterless primary constructor ([43173b8](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/43173b8722eb1d8b37c108fe84523a3d006bfae9))
* *(grammar)* Allow member access on a quotation literal ([f4c2ae4](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/f4c2ae41519636258c62f0ff0e99a23e58335926))
* *(grammar)* Prefer type application over comparison and accept untyped quotation arguments ([5703b43](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/5703b430a38ee9c84e0915584879df4ce2106700))
* *(grammar)* Reserve if/then/elif/else and repair the layouts that hid them ([8199c99](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/8199c990e2c123e81ea6f0efa1e496c6150de467))
* *(scanner)* Terminate a statement at a semicolon before a declaration ([ca7244d](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/ca7244d28e7bb810e8a2e4a5afb5fde73ae5a315))
* *(scanner)* Read the line's first word once when deciding a layout separator ([380fdec](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/380fdec099ef62a8c1d3cc637bcf0cd5cc5f2aea))
* *(scanner)* Close a type body before the next type and keep a dedented case bar ([282447c](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/282447c4dc9622577c7e00315467762c374da75c))
* *(scanner)* Lex a trailing-dot float at the start of a continuation line ([52bb5a5](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/52bb5a5a55d72e71c2bb110aab53bb78b0af6c5a))
* *(scanner)* End an inline body at a trailing semicolon before a closer ([e4b03eb](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/e4b03eb37645d8c057c431bb78e4059780fbeeaf))
* *(scanner)* Close inline try bodies before with and arm bodies before in ([9dc42fb](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/9dc42fb12df3c576fea7700bfcb5bf9d9e57c803))
* *(scanner)* Continue on a leading ascription, ?-operator, or mildly dedented infix ([62d0a29](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/62d0a29bb420a709d6421b4bcbe056461403b0ba))
* *(scanner)* Honour strings inside block comments and close arm lists at |] ([ad6244b](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/ad6244bdbed8c4b9bfb9d6dee8f1e6a81099335d))
* *(scanner)* Parse parenthesised and do bodies as layout blocks ([e03a160](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/e03a160d458811ce9f9caa635a0dafb9f2828f7e))
* *(scanner)* Dangling else claims, infix let blocks, ctor then and comment-only lines ([78ef7e0](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/78ef7e012a8ee065ab0b3359b8b431f905352305))
* *(scanner)* Done, let-in inside CE bodies, in after match values, next-line copy-update ([3403f23](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/3403f23d78e60e68c6ccd36f9cbb9803c4f39081))
* *(scanner)* As-aliased constructor let names, deref copy-update bases, in after nested arms ([9361d55](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/9361d55125acfb3176c8780c73fbc3485fb33970))
* *(scanner)* Drop strcpy so the Wasm build loads in Zed ([41acd84](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/41acd84041b932bce45418c66bcd8c53feb128dc))

### ⚡ Performance Improvements

* *(grammar)* Drop the additions that cost the most parser states ([e84e0b9](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/e84e0b95e18b9a6d085124e1555956752b558a63))
* *(grammar)* Share body fragments and split declaration rules to shrink the parser ([f154db9](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/f154db90a0c3e5255e59e5d17cd15cb97d768cec))
* *(grammar)* Split let, use and constructor right-hand sides into shared rules ([ccc1cf4](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/ccc1cf4f5990f2493226b45b8e8d343932f339d6))
* *(grammar)* Split member, val, abstract, module, accessor and extension tails into shared rules ([12f4424](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/12f4424c3aae3ad6122bc61289f7fdc803e8c528))
* *(grammar)* Lex | null as one token and fold nullable types into type expressions ([6d5e0db](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/6d5e0db9ff88a8425f8031ebf6442ce61737264b))
* *(grammar)* Share the SRTP member signature and the for binder ([b6f1fd0](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/b6f1fd0c97dbaa876712a617414a5fedc3cefa25))
* *(grammar)* Lex the simple query operators as one token ([9093f33](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/9093f33557a70a889322d180a6db4d92fbb1000d))
* *(grammar)* Share the atomic type heads through one hidden rule ([1d57559](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/1d575590e53282bc2df502af55ec58c1feef909f))
* *(scanner)* Gate labelled types on an ident-colon lookahead ([7d47010](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/7d4701061311ceae1a708b295f6fedf312f27937))
* *(scanner)* Compute the mid-line column only when a close records it ([9f15966](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/9f15966976f0e96c40b8be1efdf4b922d29ec0c9))

<strong><small>[View changes on Github](https://github.com/MangelMaxime/tree-sitter-fsharp/compare/433ada2f621f7f2f28b01d47a9478b6576659cdf..ebeef78821a64ed105d48cfaf198ffdfd833e92a)</small></strong>

## 0.0.0
