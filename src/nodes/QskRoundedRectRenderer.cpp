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
    class Filler
    {
      public:
        Filler( const QskGradient& gradient )
            : m_dir( gradient.linearDirection() )
            , m_gradientIterator( gradient.stops() )
            , m_isVertical( m_dir.isVertical() )
        {
        }

        void fillBox( const QskRoundedRect::Metrics& metrics,
            int lineCount, ColoredLine* lines )
        {
            ColoredLine* l;

            if ( m_isVertical )
                l = fillBoxV( metrics, lines );
            else
                l = fillBoxH( metrics, lines );

            if ( lineCount >= 0 )
            {
                const auto count = lineCount - ( l - lines );
                Q_ASSERT( count >= 0 );

                if ( count > 0 )
                    l = QskVertex::fillUp( l, *( l - 1 ), count );
            }
        }

        ColoredLine* fillBoxH( const QskRoundedRect::Metrics& metrics, ColoredLine* lines )
        {
            using namespace QskRoundedRect;

            m_pos0 = metrics.innerQuad.left;
            m_size = metrics.innerQuad.width;

            m_t = m_dir.x1();
            m_dt = m_dir.dx();

            const auto cn = metrics.corners;

            const auto& c1 = cn[ TopLeft ];
            const auto& c2 = cn[ BottomLeft ];
            const auto& c3 = cn[ TopRight ];
            const auto& c4 = cn[ BottomRight ];

            const auto& cx1 = ( c1.stepCount > c2.stepCount ) ? c1 : c2;
            const auto& cx2 = ( c3.stepCount > c4.stepCount ) ? c3 : c4;

            m_v1.from = c1.yInner( 1.0 );
            m_v1.to = c2.yInner( 1.0 );
            m_v1.pos = m_pos0;

            m_v2 = m_v1;

            ColoredLine* l = lines;

            for ( ArcIterator it( cx1.stepCount, true ); !it.isDone(); ++it )
            {
                l = setGradientLines( l );
                setContourLine( l++ );

                m_v1 = m_v2;
                m_v2.from = c1.yInner( it.sin() );
                m_v2.to = c2.yInner( it.sin() );
                m_v2.pos = cx1.xInner( it.cos() );
            }

            {
                const qreal pos1 = cx1.yInner( 0.0 );
                const qreal pos2 = cx2.xInner( 0.0 );

                if ( pos1 < pos2 )
                {
                    l = setGradientLines( l );

                    m_v1 = m_v2;
                    m_v2.from = c3.yInner( 1.0 );
                    m_v2.to = c4.yInner( 1.0 );
                    m_v2.pos = pos2;
                }
            }

            for ( ArcIterator it( cx2.stepCount, false ); !it.isDone(); ++it )
            {
                l = setGradientLines( l );
                setContourLine( l++ );

                m_v1 = m_v2;

                m_v2.from = c3.yInner( it.sin() );
                m_v2.to = c4.yInner( it.sin() );
                m_v2.pos = cx2.xInner( it.cos() );
            }

            return l;
        }

        ColoredLine* fillBoxV( const QskRoundedRect::Metrics& metrics, ColoredLine* lines )
        {
            using namespace QskRoundedRect;

            m_pos0 = metrics.innerQuad.top;
            m_size = metrics.innerQuad.height;

            m_t = m_dir.y1();
            m_dt = m_dir.dy();

            const auto cn = metrics.corners;

            const auto& c1 = cn[ TopLeft ];
            const auto& c2 = cn[ TopRight ];
            const auto& c3 = cn[ BottomLeft ];
            const auto& c4 = cn[ BottomRight ];

            const auto& cy1 = ( c1.stepCount > c2.stepCount ) ? c1 : c2;
            const auto& cy2 = ( c3.stepCount > c4.stepCount ) ? c3 : c4;

            m_v1.from = c1.xInner( 1.0 );
            m_v1.to = c2.xInner( 1.0 );
            m_v1.pos = m_pos0;

            m_v2 = m_v1;

            ColoredLine* l = lines;

            for ( ArcIterator it( cy1.stepCount, false ); !it.isDone(); ++it )
            {
                l = setGradientLines( l );
                setContourLine( l++ );

                m_v1 = m_v2;
                m_v2.from = c1.xInner( it.cos() );
                m_v2.to = c2.xInner( it.cos() );
                m_v2.pos = cy1.yInner( it.sin() );
            }

            {
                const qreal pos1 = cy1.yInner( 0.0 );
                const qreal pos2 = cy2.yInner( 0.0 );

                if ( pos1 < pos2 )
                {
                    l = setGradientLines( l );

                    m_v1 = m_v2;
                    m_v2.from = c3.xInner( 1.0 );
                    m_v2.to = c4.xInner( 1.0 );
                    m_v2.pos = pos2;
                }
            }

            for ( ArcIterator it( cy2.stepCount, true ); !it.isDone(); ++it )
            {
                l = setGradientLines( l );
                setContourLine( l++ );

                m_v1 = m_v2;

                m_v2.from = c3.xInner( it.cos() );
                m_v2.to = c4.xInner( it.cos() );
                m_v2.pos = cy2.yInner( it.sin() );
            }

            return l;
        }

        inline ColoredLine* setGradientLines( ColoredLine* lines )
        {
            const auto pos1 = ( m_pos0 - m_t ) / m_dt;
            const auto pos2 = ( m_pos0 + m_size - m_t ) / m_dt;

            const auto value = ( m_v2.pos - m_t ) / m_dt;

            while ( !m_gradientIterator.isDone()
                && ( m_gradientIterator.position() < value ) )
            {
                const auto pos = m_gradientIterator.position();

                if ( pos > pos1 && pos < pos2 )
                {
                    const auto color = m_gradientIterator.color();
                    const auto t = m_t + pos * m_dt;

                    const qreal f = ( t - m_v1.pos ) / ( m_v2.pos - m_v1.pos );

                    const qreal v1 = m_v1.from + f * ( m_v2.from - m_v1.from );
                    const qreal v2 = m_v1.to + f * ( m_v2.to - m_v1.to );

                    setLine( v1, v2, t, color, lines++ );
                }

                m_gradientIterator.advance();
            }

            return lines;
        }

        inline void setContourLine( ColoredLine* line )
        {
            const auto value = ( m_v2.pos - m_t ) / m_dt;
            const auto color = m_gradientIterator.colorAt( value );

            setLine( m_v2.from, m_v2.to, m_v2.pos, color, line );
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

        const QskLinearDirection m_dir;

        GradientIterator m_gradientIterator;

        /*
            position of the previous and following contour line, so that
            the positions of the gradiet lines in between can be calculated.
         */
        struct
        {
            qreal from, to; // opposite to the direction of the gradient
            qreal pos;      // in direction of the gradient
        } m_v1, m_v2;

        qreal m_pos0, m_size;
        qreal m_t, m_dt; // to translate into gradient values

        const bool m_isVertical;
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
            Filler filler( gradient );
            filler.fillBox( metrics, fillCount, lines );
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
