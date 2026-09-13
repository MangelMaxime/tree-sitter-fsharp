module EasyBuild.Workspace

open EasyBuild.FileSystemProvider

[<Literal>]
let Root = __SOURCE_DIRECTORY__ + "/../"

type Workspace = AbsoluteFileSystem<Root>
