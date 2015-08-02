// Guids.cs
// MUST match guids.h
using System;

namespace Microsoft.VSPackage1
{
    static class GuidList
    {
        public const string guidVSPackage1PkgString = "27d97bf0-ec8c-466d-b1a0-df926943c05e";
        public const string guidVSPackage1CmdSetString = "bf9bc06c-aa1a-44ff-9ff0-c30d042ce66c";

        public static readonly Guid guidVSPackage1CmdSet = new Guid(guidVSPackage1CmdSetString);
    };
}