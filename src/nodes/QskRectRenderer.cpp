/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskBoxBorderColors.h"
#include "QskBoxBorderMetrics.h"
#include "QskRectRenderer.h"
#include "QskBoxRendererColorMap.h"
#include "QskGradient.h"
#include "QskGradientDirection.h"
#include "QskFunctions.h"
#include "QskVertex.h"

using namespace QskVertex;

namespace
{
    class Stroker
    {
      public:
        Stroker( const QRectF& rect, const QskBoxBorderMetrics& border,
                const QskBoxBorderColors& borderColors, const QskGradient& gradient )
            : m_in( qskValidOrEmptyInnerRect( rect, border.widths() ) )
            , m_out( rect )
            , m_borderColors( borderColors )
            , m_gradient( gradient )
            , m_isColored( true )
        {
        }

        Stroker( const Quad& rect, const QskGradient& gradient )
            : m_in( rect )
            , m_out( rect )
            , m_gradient( gradient )
            , m_isColored( true )
        {
        }

        Stroker( const QRectF& rect, const QskBoxBorderMetrics& border )
            : m_in( qskValidOrEmptyInnerRect( rect, border.widths() ) )
            , m_out( rect )
            , m_isColored( false )
        {
        }

        int borderCount() const
        {
            if ( m_in == m_out )
                return 0;

            // We can build a rectangular border from the 4 diagonal
            // lines at the corners, but need an additional line
            // for closing the border.

            int n = 5;

            if ( m_isColored )
            {
                if ( !m_borderColors.isVisible() )
                    return 0;

                if ( !m_borderColors.isMonochrome() )
                {
                    const int gradientLines = -1
                        + m_borderColors.left().stepCount()
                        + m_borderColors.top().stepCount()
                        + m_borderColors.right().stepCount()
                        + m_borderColors.bottom().stepCount();

                    n += qMax( gradientLines, 0 );
                }
            }

            return n;
        }

        int fillCount() const
        {
            if ( m_in.isEmpty() || ( m_isColored && !m_gradient.isVisible() ) )
                return 0;

            int n = 2;

            if ( m_isColored && !m_gradient.isMonochrome() )
            {
                const auto dir = m_gradient.linearDirection();
                if ( dir.isTilted() )
                    n += 2; // contour lines for the opposite corners

                n += m_gradient.stepCount() - 1;

                if ( !m_gradient.linearDirection().contains( m_in ) )
                {
                    /*
                        The gradient starts and/or ends inside of the rectangle
                        and we have to add extra gradient lines. As this is a
                        corner case we always allocate memory for both to avoid
                        making this calculation even more confusing.
                     */

                    n += 2;
                }
            }

            return n;
        }

        ColoredLine* setBorderLines( ColoredLine* lines ) const
        {
            const QLineF cl[4] =
            {
                { m_in.right, m_in.bottom, m_out.right, m_out.bottom },
                { m_in.left, m_in.bottom, m_out.left, m_out.bottom },
                { m_in.left, m_in.top, m_out.left, m_out.top },
                { m_in.right, m_in.top, m_out.right, m_out.top }
            };

            if ( m_borderColors.isMonochrome() )
            {
                const Color c = m_borderColors.left().rgbStart();

                lines[0].setLine( cl[0], c );
                lines[1].setLine( cl[1], c );
                lines[2].setLine( cl[2], c );
                lines[3].setLine( cl[3], c );
                lines[4] = lines[ 0 ];

                return lines + 5;
            }
            else
            {
                using namespace QskVertex;

                lines = addGradientLines( cl[0], cl[1], m_borderColors.bottom(), lines );
                lines = addGradientLines( cl[1], cl[2], m_borderColors.left(), lines );
                lines = addGradientLines( cl[2], cl[3], m_borderColors.top(), lines );
                lines = addGradientLines( cl[3], cl[0], m_borderColors.right(), lines );
            }

            return lines;
        }

        Line* setBorderLines( Line* lines ) const
        {
            lines[0].setLine( m_in.right, m_in.bottom, m_out.right, m_out.bottom );
            lines[1].setLine( m_in.left, m_in.bottom, m_out.left, m_out.bottom );
            lines[2].setLine( m_in.left, m_in.top, m_out.left, m_out.top );
            lines[3].setLine( m_in.right, m_in.top, m_out.right, m_out.top );
            lines[4] = lines[ 0 ];

            return lines + 5;
        }

        void setFillLines( QskVertex::Line* lines ) const
        {
            lines[0].setLine( m_in.left, m_in.top, m_in.right, m_in.top );
            lines[1].setLine( m_in.left, m_in.bottom, m_in.right, m_in.bottom );
        }

        void setFillLines( QskVertex::ColoredLine* lines, int lineCount ) const
        {
            if ( m_gradient.isMonochrome() )
            {
                const QskVertex::ColorMap map( m_gradient );

                map.setLine( m_in.left, m_in.top, m_in.right, m_in.top, lines + 0 );
                map.setLine( m_in.left, m_in.bottom, m_in.right, m_in.bottom, lines + 1 );

                return;
            }

            GradientIterator it( m_gradient.stops() );
            ColoredLine* l = lines;

            const auto dir = m_gradient.linearDirection();

            if ( dir.isTilted() )
            {
                const qreal m = dir.dy() / dir.dx();
                const auto vec = dir.vector();

                struct { qreal x, y, value; } c1, c2, c3, c4;

                {
                    // corners sorted in order their values
                    c1 = { m_in.left, m_in.top, dir.valueAt( m_in.left, m_in.top ) };
                    c2 = { m_in.right, m_in.top, dir.valueAt( m_in.right, m_in.top ) };
                    c3 = { m_in.left, m_in.bottom, dir.valueAt( m_in.left, m_in.bottom ) };
                    c4 = { m_in.right, m_in.bottom, dir.valueAt( m_in.right, m_in.bottom ) };

                    if ( m < 0.0 )
                    {
                        qSwap( c1, c3 );
                        qSwap( c2, c4 );
                    }

                    if ( c1.value > c4.value )
                        qSwap( c1, c4 );

                    if ( c2.value > c3.value )
                        qSwap( c2, c3 );
                }

                // skipping all gradient lines before the first corner
                while ( !it.isDone() && ( it.position() <= c1.value ) )
                    it.advance();

                setLine( c1.x, c1.y, c1.x, c1.y, it.colorAt( c1.value ), l++ );

                while ( !it.isDone() && ( it.position() < c2.value ) )
                {
                    const auto p = vec.pointAt( it.position() );

                    const qreal y1 = p.y() + ( p.x() - c1.x ) / m;
                    const qreal x2 = p.x() + ( p.y() - c1.y ) * m;

                    setLine( c1.x, y1, x2, c1.y, it.color(), l++ );
                    it.advance();
                }

                if ( c1.x == c3.x ) // cutting left/right edges
                {
                    const auto dy = ( c2.x - c3.x ) / m;

                    setLine( c2.x, c2.y, c3.x, c2.y + dy, it.colorAt( c2.value ), l++ );

                    while ( !it.isDone() && ( it.position() < c3.value ) )
                    {
                        const auto p = vec.pointAt( it.position() );

                        const qreal y1 = p.y() + ( p.x() - c2.x ) / m;
                        const qreal y2 = p.y() + ( p.x() - c3.x ) / m;

                        setLine( c2.x, y1, c3.x, y2, it.color(), l++ );
                        it.advance();
                    }

                    setLine( c2.x, c3.y - dy, c3.x, c3.y, it.colorAt( c3.value ), l++ );
                }
                else // cutting top/bottom edges
                {
                    const qreal dx = ( c2.y - c3.y ) * m;

                    setLine( c2.x, c2.y, c2.x + dx, c3.y, it.colorAt( c2.value ), l++ );

                    while ( !it.isDone() && ( it.position() < c3.value ) )
                    {
                        const auto p = vec.pointAt( it.position() );

                        const qreal x1 = p.x() + ( p.y() - c2.y ) * m;
                        const qreal x2 = p.x() + ( p.y() - c3.y ) * m;

                        setLine( x1, c2.y, x2, c3.y, it.color(), l++ );
                        it.advance();
                    }

                    setLine( c3.x - dx, c2.y, c3.x, c3.y, it.colorAt( c3.value ), l++ );
                }

                while ( !it.isDone() && ( it.position() < c4.value ) )
                {
                    const auto p = vec.pointAt( it.position() );

                    const qreal y1 = p.y() + ( p.x() - c4.x ) / m;
                    const qreal x2 = p.x() + ( p.y() - c4.y ) * m;

                    setLine( c4.x, y1, x2, c4.y, it.color(), l++ );
                    it.advance();
                }

                setLine( c4.x, c4.y, c4.x, c4.y, it.colorAt( c4.value ), l++ );
            }
            else if ( dir.isVertical() )
            {
                Q_ASSERT( dir.dy() > 0.0 ); // normalized in QskBoxRenderer

                const qreal min = ( m_in.top - dir.y1() ) / dir.dy();
                const qreal max = ( m_in.bottom - dir.y1() ) / dir.dy();

                while ( !it.isDone() && ( it.position() <= min ) )
                    it.advance();

                setHLine( m_in.top, it.colorAt( min ), l++ );

                while ( !it.isDone() && ( it.position() < max ) )
                {
                    const auto y = dir.y1() + it.position() * dir.dy();
                    setHLine( y, it.color(), l++ );

                    it.advance();
                }

                setHLine( m_in.bottom, it.colorAt( max ), l++ );
            }
            else // dir.isHorizontal
            {
                Q_ASSERT( dir.dx() > 0.0 ); // normalized in QskBoxRenderer

                const qreal min = ( m_in.left - dir.x1() ) / dir.dx();
                const qreal max = ( m_in.right - dir.x1() ) / dir.dx();

                while ( !it.isDone() && ( it.position() <= min ) )
                    it.advance();

                setVLine( m_in.left, it.colorAt( min ), l++ );

                while ( !it.isDone() && ( it.position() < max ) )
                {
                    const auto x = dir.x1() + it.position() * dir.dx();
                    setVLine( x, it.color(), l++ );

                    it.advance();
                }

                setVLine( m_in.right, it.colorAt( max ), l++ );
            }

            if ( lineCount )
            {
                const auto count = lineCount - ( l - lines );
                Q_ASSERT( count >= 0 );

                if ( count > 0 )
                {
                    /*
                        When there are gradiet stops outside of the rectangle
                        we have allocated too much memory. Maybe we work
                        on this later, but for the moment we simply
                        duplicate the last line.
                     */
                    QskVertex::fillUp( l, *( l - 1 ), count );
                }
            }
        }

      private:

        inline void setHLine( qreal y, Color color, QskVertex::ColoredLine* line ) const
        {
            line->setLine( m_in.left, y, m_in.right, y, color );
        }

        inline void setVLine( qreal x, Color color, QskVertex::ColoredLine* line ) const
        {
            line->setLine( x, m_in.top, x, m_in.bottom, color );
        }

        inline void setLine( qreal x1, qreal y1, qreal x2, qreal y2,
            Color color, QskVertex::ColoredLine* line ) const
        {
            if ( x1 <= x2 )
                line->setLine( x1, y1, x2, y2, color );
            else
                line->setLine( x2, y2, x1, y1, color );
        }

      private:
        const Quad m_in, m_out;
        const QskBoxBorderColors m_borderColors;
        const QskGradient m_gradient;

        const bool m_isColored;
    };
}

void QskRectRenderer::renderBorderGeometry( const QRectF& rect,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    const Stroker stroker( rect, border );

    const auto lines = allocateLines< Line >( geometry, stroker.borderCount() );
    if ( lines )
        stroker.setBorderLines( lines );
}

void QskRectRenderer::renderFillGeometry( const QRectF& rect,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    const Stroker stroker( rect, border );

    if ( const auto lines = allocateLines< Line >( geometry, stroker.fillCount() ) )
        stroker.setFillLines( lines );
}

void QskRectRenderer::renderRect( const QRectF& rect,
    const QskBoxBorderMetrics& border, const QskBoxBorderColors& borderColors,
    const QskGradient& gradient, QSGGeometry& geometry )
{
    const Stroker stroker( rect, border, borderColors, gradient );

    const int fillCount = stroker.fillCount();
    const int borderCount = stroker.borderCount();

    const auto lines = allocateLines< ColoredLine >(
        geometry, borderCount + fillCount );

    if ( fillCount )
        stroker.setFillLines( lines, fillCount );

    if ( borderCount )
        stroker.setBorderLines( lines + fillCount );
}

void QskRectRenderer::renderFill0( const QskVertex::Quad& rect,
    const QskGradient& gradient, int lineCount, QskVertex::ColoredLine* lines )
{
    const Stroker stroker( rect, gradient );
    stroker.setFillLines( lines, lineCount );
}
