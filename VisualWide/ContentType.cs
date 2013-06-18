using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Linq;
using System.Threading;
using System.Windows.Media;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Text.Operations;
using Microsoft.VisualStudio.Text.Tagging;
using Microsoft.VisualStudio.Utilities;
using System.Runtime.InteropServices;

namespace VisualWide
{
    internal class ContentType
    {
        [Export]
        [Name("Wide")]
        [BaseDefinition("code")]
        internal static ContentTypeDefinition WideContentType = null;

        [Export]
        [ContentType("Wide")]
        [FileExtension(".wide")]
        internal static FileExtensionToContentTypeDefinition WideFileType = null;
    }
}
