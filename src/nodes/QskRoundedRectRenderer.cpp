/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskRoundedRectRenderer.h"

#include "QskBoxBorderColors.h"
#include "QskBoxBorderMetrics.h"
#include "QskBoxRendererColorMap.h"
#include "QskBoxShapeMetrics.h"
#include "QskGradientDirection.h"
#include "QskRectRenderer.h"
#include "QskRoundedRect.h"

#include <qmath.h>
#include <qsggeometry.h>

using namespace QskVertex;

namespace
{
    using namespace QskRoundedRect;

    class Value
    {
      public:
        qreal from, to; // opposite to the direction of the gradient
        qreal pos;      // in direction of the gradient
    };

    class ContourIterator
    {
      public:
        ContourIterator( const Metrics& metrics )
            : m_metrics( metrics )
        {
        }

        inline void reset( int c1, int c2, bool isOpen )
        {
            m_isVertical = ( c1 == BottomLeft ) || ( c2 == TopRight );
            m_c1 = c1;
            m_c2 = c2;

            const auto cn = m_metrics.corners;
            m_c3 = ( cn[c1].stepCount > cn[c2].stepCount ) ? c1 : c2;

            m_it.reset( cn[m_c3].stepCount, m_isVertical != isOpen );
        }

        inline bool advance()
        {
            ++m_it;
            return !m_it.isDone();
        }

        inline Value value() const
        {
            const auto cos = m_it.cos();
            const auto sin = m_it.sin();

            const auto& c1 = m_metrics.corners[ m_c1 ];
            const auto& c2 = m_metrics.corners[ m_c2 ];
            const auto& c3 = m_metrics.corners[ m_c3 ];

            if ( m_isVertical )
                return { c1.xInner( cos ), c2.xInner( cos ), c3.yInner( sin ) };
            else
                return { c1.yInner( sin ), c2.yInner( sin ), c3.xInner( cos ) };
        }

      private:
        const Metrics& m_metrics;

        bool m_isVertical;

        int m_c1, m_c2, m_c3;
        ArcIterator m_it;
    };

    class Filler
    {
      public:
        void fillBox( const QskRoundedRect::Metrics& metrics,
            const QskGradient& gradient, int lineCount, ColoredLine* lines )
        {
            const int* corners;

            const auto dir = gradient.linearDirection();

            m_isVertical = dir.isVertical();

            if ( m_isVertical )
            {
                using namespace QskRoundedRect;
                static const int c[] = { TopLeft, TopRight, BottomLeft, BottomRight };
                corners = c;

                m_t0 = dir.y1();
                m_dt = dir.dy();
            }
            else
            {
                using namespace QskRoundedRect;
                static const int c[] = { TopLeft, BottomLeft, TopRight, BottomRight };
                corners = c;

                m_t0 = dir.x1();
                m_dt = dir.dx();
            }

            ColoredLine* l = lines;
            Value v1, v2;

            m_gradientIterator.reset( gradient.stops() );
            ContourIterator it( metrics );

            it.reset( corners[0], corners[1], true );

            v2 = it.value();

            skipGradientLines( v2.pos );
            setContourLine( v2, l++ );

            while( it.advance() )
            {
                v1 = v2;
                v2 = it.value();

                l = setGradientLines( v1, v2, l );
                setContourLine( v2, l++ );
            };

            it.reset( corners[2], corners[3], false );

            if ( it.value().pos <= v2.pos ) // ellipse
                it.advance();

            do
            {
                v1 = v2;
                v2 = it.value();

                l = setGradientLines( v1, v2, l );
                setContourLine( v2, l++ );

            } while( it.advance() );

            if ( lineCount >= 0 )
            {
                const auto count = lineCount - ( l - lines );

                Q_ASSERT( count >= 0 );

                if ( count > 0 )
                    l = QskVertex::fillUp( l, *( l - 1 ), count );
            }
        }

        inline void skipGradientLines( qreal pos )
        {
            while ( !m_gradientIterator.isDone() )
            {
                if ( m_t0 + m_gradientIterator.position() * m_dt > pos )
                    return;

                m_gradientIterator.advance();
            }
        }

        inline ColoredLine* setGradientLines(
            const Value& v1, const Value& v2, ColoredLine* lines )
        {
            while ( !m_gradientIterator.isDone() )
            {
                const auto pos = m_t0 + m_gradientIterator.position() * m_dt;

                if ( pos >= v2.pos )
                    return lines;

                const auto color = m_gradientIterator.color();

                const qreal f = ( pos - v1.pos ) / ( v2.pos - v1.pos );

                const qreal t1 = v1.from + f * ( v2.from - v1.from );
                const qreal t2 = v1.to + f * ( v2.to - v1.to );

                setLine( t1, t2, pos, color, lines++ );

                m_gradientIterator.advance();
            }

            return lines;
        }

        inline void setContourLine( const Value& v, ColoredLine* line )
        {
            const auto color = m_gradientIterator.colorAt( ( v.pos - m_t0 ) / m_dt );
            setLine( v.from, v.to, v.pos, color, line );
        }

      private:
        inline void setLine( qreal from, qreal to, qreal pos,
            Color color, ColoredLine* line )
        {
            if ( m_isVertical )
                line->setLine( from, pos, to, pos, color );
            else
                line->setLine( pos, from, pos, to, color );
        }

        bool m_isVertical;
        qreal m_t0, m_dt;

        GradientIterator m_gradientIterator;
    };
}

static inline bool qskGradientLinesNeeded(
    const QRectF& rect, const QskGradient& gradient )
{
    if ( gradient.isMonochrome() )
        return false;

    switch( gradient.stepCount() )
    {
        case 0:
            return false;

        case 1:
        {
            Q_ASSERT( gradient.stretchMode() != QskGradient::StretchToSize );
            return !gradient.linearDirection().contains( rect );
        }

        default:
            return true;
    }
}

void QskRoundedRectRenderer::renderBorderGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    const QskRoundedRect::Metrics metrics( rect, shape, border );

    if ( metrics.innerQuad == metrics.outerQuad )
    {
        allocateLines< Line >( geometry, 0 );
        return;
    }

    const int stepCount = metrics.corners[ 0 ].stepCount;
    const int lineCount = 4 * ( stepCount + 1 ) + 1;

    const auto lines = allocateLines< Line >( geometry, lineCount );

    QskRoundedRect::Stroker stroker( metrics );
    stroker.setBorderLines( lines );
}

void QskRoundedRectRenderer::renderFillGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    using namespace QskRoundedRect;

    Stroker stroker( Metrics( rect, shape, border ) );

    if ( auto lines = allocateLines< Line >( geometry, stroker.fillCount() ) )
        stroker.setFillLines( lines );
}

void QskRoundedRectRenderer::renderRect( const QRectF& rect,
    const QskBoxShapeMetrics& shape, const QskBoxBorderMetrics& border,
    const QskBoxBorderColors& borderColors, const QskGradient& gradient,
    QSGGeometry& geometry )
{
    using namespace QskRoundedRect;

    Metrics metrics( rect, shape, border );
    const Stroker stroker( metrics, borderColors, gradient );

    if ( metrics.innerQuad.isEmpty() ||
        !qskGradientLinesNeeded( metrics.innerQuad, gradient ) )
    {
        /*
            We can do all colors with the vertexes of the contour lines
            what allows using simpler and faster algos
         */

        const int fillCount = stroker.fillCount();
        const int borderCount = stroker.borderCount();

        auto lines = allocateLines< ColoredLine >(
            geometry, borderCount + fillCount );

        auto fillLines = fillCount ? lines : nullptr;
        auto borderLines = borderCount ? lines + fillCount : nullptr;

        stroker.setBoxLines( borderLines, fillLines );

        return;
    }

    const auto dir = gradient.linearDirection();

    int gradientLineCount = gradient.stepCount() - 1;
    if ( !dir.contains( metrics.innerQuad ) )
        gradientLineCount += 2;

    if ( metrics.isTotallyCropped )
    {
        const int borderCount = stroker.borderCount();

        int fillCount = 2 + gradientLineCount;
        if ( dir.isTilted() )
            fillCount += 2;

        auto lines = allocateLines< ColoredLine >( geometry, borderCount + fillCount );

        if ( fillCount )
            QskRectRenderer::renderFill0( metrics.innerQuad, gradient, fillCount, lines );

        if ( borderCount )
            stroker.setBorderLines( lines + fillCount );
    }
    else if ( !dir.isTilted() )
    {
        const int borderCount = stroker.borderCount();
        const int fillCount = stroker.fillCount() + gradientLineCount;

        auto lines = allocateLines< ColoredLine >( geometry, borderCount + fillCount );

        metrics.preferredOrientation = dir.isVertical() ? Qt::Vertical : Qt::Horizontal;

        if ( fillCount )
        {
            Filler filler;
            filler.fillBox( metrics, gradient, fillCount, lines );
        }

        if ( borderCount )
            stroker.setBorderLines( lines + fillCount );
    }
    else
    {
        const int borderCount = stroker.borderCount();

        // why not using metrics.cornerStepCount()
        const int stepCount = metrics.corners[ 0 ].stepCount; // is this correct ???

        int fillCount = 2 + gradientLineCount + 2 * stepCount;
        fillCount *= 2;

        if ( borderCount && fillCount  )
        {
            /*
                The filling ends at 45° and we have no implementation
                for creating the border from there. So we need to
                insert an extra dummy line to connect fill and border
             */

            auto lines = allocateLines< ColoredLine >(
                geometry, fillCount + borderCount + 1 );

            renderDiagonalFill( metrics, gradient, fillCount, lines );
            stroker.setBorderLines( lines + fillCount + 1 );

            const auto l = lines + fillCount;
            l[ 0 ].p1 = l[ -1 ].p2;
            l[ 0 ].p2 = l[ 1 ].p1;
        }
        else
        {
            auto lines = allocateLines< ColoredLine >(
                geometry, fillCount + borderCount );

            renderDiagonalFill( metrics, gradient, fillCount, lines );

            if ( borderCount )
                stroker.setBorderLines( lines + fillCount );
        }
    }
}
