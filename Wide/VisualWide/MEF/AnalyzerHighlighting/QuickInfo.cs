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
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Language.Intellisense;
using System.Runtime.InteropServices;

namespace VisualWide.AnalyzerHighlighting
{
    [Export(typeof(IQuickInfoSourceProvider))]
    [Name("Wide ToolTip QuickInfo Source")]
    [Order(Before = "Default Quick Info Presenter")]
    [ContentType("Wide")]
    internal class QuickInfoSourceProvider : IQuickInfoSourceProvider
    {
        public IQuickInfoSource TryCreateQuickInfoSource(ITextBuffer textBuffer)
        {
            return new QuickInfoSource(textBuffer);
        }
    }
    internal class QuickInfoSource : IQuickInfoSource
    {
        ITextBuffer buffer;

        public QuickInfoSource(ITextBuffer buf)
        {
            buffer = buf;
        }
        public void AugmentQuickInfoSession(IQuickInfoSession session, IList<object> qiContent, out ITrackingSpan applicableToSpan)
        {
            // Map the trigger point down to our buffer.
            SnapshotPoint? subjectTriggerPoint = session.GetTriggerPoint(buffer.CurrentSnapshot);
            if (!subjectTriggerPoint.HasValue)
            {
                applicableToSpan = null;
                return;
            }
            var AnalyzerInfo = ProjectUtils.instance.GetProjectsFor(buffer)
                .Select(proj => proj.AnalyzerQuickInfo)
                .Aggregate((x, y) => x.Concat(y))
                .Where(info => info.where.Snapshot == buffer.CurrentSnapshot)
                .ToList();
            foreach(var info in AnalyzerInfo) {
                if (!info.where.Contains(subjectTriggerPoint.Value))
                    continue;
                applicableToSpan = buffer.CurrentSnapshot.CreateTrackingSpan(info.where.Span, SpanTrackingMode.EdgeInclusive);
                qiContent.Add(info.info);
                return;
            }
            applicableToSpan = null;
        }
        
        private bool m_isDisposed;
        public void Dispose()
        {
            if (!m_isDisposed)
            {
                GC.SuppressFinalize(this);
                m_isDisposed = true;
            }
        }
    }

    internal class QuickInfoController : IIntellisenseController
    {
        ITextView view;
        IList<ITextBuffer> buffers;
        IQuickInfoBroker broker;
        IQuickInfoSession session;

        public QuickInfoController(ITextView v, IList<ITextBuffer> bufs, IQuickInfoBroker brok)
        {
            view = v;
            buffers = bufs;
            broker = brok;
            view.MouseHover += this.OnTextViewMouseHover;
        }

        private void OnTextViewMouseHover(object sender, MouseHoverEventArgs e)
        {
            // No idea how this works.. it's a C&P from MSDN essentially.
            SnapshotPoint? point = view.BufferGraph.MapDownToFirstMatch
                 (new SnapshotPoint(view.TextSnapshot, e.Position),
                PointTrackingMode.Positive,
                snapshot => buffers.Contains(snapshot.TextBuffer),
                PositionAffinity.Predecessor);

            if (point != null)
            {
                ITrackingPoint triggerPoint = point.Value.Snapshot.CreateTrackingPoint(point.Value.Position,
                PointTrackingMode.Positive);

                if (!broker.IsQuickInfoActive(view))
                {
                    session = broker.TriggerQuickInfo(view, triggerPoint, true);
                }
            }
        }
        public void Detach(ITextView textView)
        {
            if (view == textView)
            {
                view.MouseHover -= this.OnTextViewMouseHover;
                view = null;
            }
        }

        public void ConnectSubjectBuffer(ITextBuffer subjectBuffer)
        {
        }

        public void DisconnectSubjectBuffer(ITextBuffer subjectBuffer)
        {
        }
    }
    
    [Export(typeof(IIntellisenseControllerProvider))]
    [Name("Wide ToolTip QuickInfo Controller")]
    [ContentType("Wide")]
    internal class QuickInfoControllerProvider : IIntellisenseControllerProvider
    {
        [Import]
        internal IQuickInfoBroker broker { get; set; }

        public IIntellisenseController TryCreateIntellisenseController(ITextView view, IList<ITextBuffer> buffers)
        {
            return new QuickInfoController(view, buffers, broker);
        }
    }
}
