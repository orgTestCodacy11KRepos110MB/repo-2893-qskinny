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
    class Vector1D
    {
      public:
        Vector1D() = default;

        inline constexpr Vector1D( qreal origin, qreal length )
            : origin( origin )
            , length( length )
        {
        }

        inline qreal valueAt( qreal t ) const
        {
            return origin + t * length;
        }

        qreal origin = 0.0;
        qreal length = 0.0;
    };

    /*
        A contour iterator for vertical and horizontal linear gradients.
        The radii in direction of the gradient need to match at the
        opening and at the closing sides,
     */
    class HVRectEllipseIterator
    {
      public:
        HVRectEllipseIterator(
                const QskRoundedRect::Metrics& metrics, const QLineF& vector )
            : m_vertical( vector.x1() == vector.x2() )
        {
            using namespace QskRoundedRect;

            const int* c;

            if ( m_vertical )
            {
                static const int cV[] = { TopLeft, TopRight, BottomLeft, BottomRight };
                c = cV;

                m_pos0 = metrics.innerQuad.top;
                m_size = metrics.innerQuad.height;

                m_t = vector.y1();
                m_dt = vector.dy();
            }
            else
            {
                static const int cH[] = { TopLeft, BottomLeft, TopRight, BottomRight };
                c = cH;

                m_pos0 = metrics.innerQuad.left;
                m_size = metrics.innerQuad.width;

                m_t = vector.x1();
                m_dt = vector.dx();
            }

            const auto& mc1 = metrics.corners[ c[0] ];
            const auto& mc2 = metrics.corners[ c[1] ];
            const auto& mc3 = ( mc1.stepCount >= mc2.stepCount ) ? mc1 : mc2;

            const auto& mc4 = metrics.corners[ c[2] ];
            const auto& mc5 = metrics.corners[ c[3] ];
            const auto& mc6 = ( mc4.stepCount >= mc5.stepCount ) ? mc4 : mc5;

            m_vector[0] = vectorAt( !m_vertical, false, mc1 );
            m_vector[1] = vectorAt( !m_vertical, true, mc2 );
            m_vector[2] = vectorAt( m_vertical, false, mc3 );

            m_vector[3] = vectorAt( !m_vertical, false, mc4 );
            m_vector[4] = vectorAt( !m_vertical, true, mc5 );
            m_vector[5] = vectorAt( m_vertical, true, mc6 );

            m_stepCounts[0] = mc3.stepCount;
            m_stepCounts[1] = mc6.stepCount;

            m_v1.from = m_vector[0].valueAt( 1.0 );
            m_v1.to = m_vector[1].valueAt( 1.0 );
            m_v1.pos = m_pos0;

            m_v2 = m_v1;

            m_arcIterator.reset( m_stepCounts[0], false );
        }

        inline bool advance()
        {
            auto v = m_vector;

            if ( m_arcIterator.step() == m_arcIterator.stepCount() )
            {
                if ( m_arcIterator.isInverted() )
                {
                    // we have finished the closing "corners"
                    return false;
                }

                m_arcIterator.reset( m_stepCounts[1], true );

                const qreal pos1 = v[2].valueAt( 0.0 );
                const qreal pos2 = v[5].valueAt( 0.0 );

                if ( pos1 < pos2 )
                {
                    // the real rectangle - between the rounded "corners "
                    m_v1 = m_v2;

                    m_v2.from = v[3].valueAt( 1.0 );
                    m_v2.to = v[4].valueAt( 1.0 );
                    m_v2.pos = pos2;

                    return true;
                }
            }

            m_arcIterator.increment();

            m_v1 = m_v2;

            if ( m_arcIterator.isInverted() )
                v += 3;

            m_v2.from = v[0].valueAt( m_arcIterator.cos() );
            m_v2.to = v[1].valueAt( m_arcIterator.cos() );
            m_v2.pos = v[2].valueAt( m_arcIterator.sin() );

            return true;
        }

        inline void setGradientLine( qreal value, Color color, ColoredLine* line )
        {
            const auto pos = m_t + value * m_dt;

            const qreal f = ( pos - m_v1.pos ) / ( m_v2.pos - m_v1.pos );

            const qreal v1 = m_v1.from + f * ( m_v2.from - m_v1.from );
            const qreal v2 = m_v1.to + f * ( m_v2.to - m_v1.to );

            setLine( v1, v2, pos, color, line );
        }

        inline void setContourLine( Color color, ColoredLine* line )
        {
            setLine( m_v2.from, m_v2.to, m_v2.pos, color, line );
        }

        inline qreal valueBegin() const { return ( m_pos0 - m_t ) / m_dt; }
        inline qreal valueEnd() const { return ( ( m_pos0 + m_size ) - m_t ) / m_dt; }
        inline qreal value() const { return ( m_v2.pos - m_t ) / m_dt; }

      private:

        inline Vector1D vectorAt( bool vertical, bool increasing,
            const QskRoundedRect::Metrics::Corner& c ) const
        {
            qreal center, radius;

            if ( vertical )
            {
                center = c.centerY;
                radius = c.radiusInnerY;
            }
            else
            {
                center = c.centerX;
                radius = c.radiusInnerX;
            }

            const qreal f = increasing ? 1.0 : -1.0;

            if ( radius < 0.0 )
            {
                center += radius * f;
                radius = 0.0;
            }
            else
            {
                radius *= f;
            }

            return { center, radius };
        }

        inline void setLine( qreal from, qreal to, qreal pos,
            Color color, ColoredLine* line )
        {
            if ( m_vertical )
                line->setLine( from, pos, to, pos, color );
            else
                line->setLine( pos, from, pos, to, color );
        }

        const bool m_vertical;

        int m_stepCounts[2];
        ArcIterator m_arcIterator;

        /*
            This iterator supports shapes, where we have the same radius in
            direction of the gradient ( exception: one corner is not rounded ).
            However we allow having different radii opposite to the direction
            of the gradient. So we have 3 center/radius pairs to calculate the
            interpolating contour lines at both ends ( opening/closing ).
         */
        Vector1D m_vector[6];

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
    stroker.createBorderLines( lines );
}

void QskRoundedRectRenderer::renderFillGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    using namespace QskRoundedRect;

    Stroker stroker( Metrics( rect, shape, border ) );

    if ( auto lines = allocateLines< Line >( geometry, stroker.fillLineCount() ) )
        stroker.createFillLines( lines );
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

        const int fillLineCount = stroker.fillLineCount();
        const int borderLineCount = stroker.borderLineCount();

        auto lines = allocateLines< ColoredLine >(
            geometry, borderLineCount + fillLineCount );

        auto fillLines = fillLineCount ? lines : nullptr;
        auto borderLines = borderLineCount ? lines + fillLineCount : nullptr;

        stroker.createBox( borderLines, fillLines );

        return;
    }

    const auto dir = gradient.linearDirection();

    int gradientLineCount = gradient.stepCount() - 1;
    if ( !dir.contains( metrics.innerQuad ) )
        gradientLineCount += 2;

    if ( metrics.isTotallyCropped )
    {
        const int borderCount = stroker.borderLineCount();

        int fillCount = 2 + gradientLineCount;
        if ( dir.isTilted() )
            fillCount += 2;

        auto lines = allocateLines< ColoredLine >( geometry, borderCount + fillCount );

        if ( fillCount )
            QskRectRenderer::renderFill0( metrics.innerQuad, gradient, fillCount, lines );

        if ( borderCount )
            stroker.createBorder( lines + fillCount );
    }
    else if ( !dir.isTilted() )
    {
        const int borderCount = stroker.borderLineCount();
        const int fillCount = stroker.fillLineCount() + gradientLineCount;

        auto lines = allocateLines< ColoredLine >( geometry, borderCount + fillCount );

        metrics.preferredOrientation = dir.isVertical() ? Qt::Vertical : Qt::Horizontal;

        if ( fillCount )
        {
            HVRectEllipseIterator it( metrics, dir.vector() );
            QskVertex::fillBox( it, gradient, fillCount, lines );
        }

        if ( borderCount )
            stroker.createBorder( lines + fillCount );
    }
    else
    {
        const int borderCount = stroker.borderLineCount();

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
            stroker.createBorder( lines + fillCount + 1 );

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
                stroker.createBorder( lines + fillCount );
        }
    }
}
