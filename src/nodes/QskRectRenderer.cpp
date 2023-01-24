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
    class BorderStroker
    {
      public:
        BorderStroker( const Quad& in, const Quad& out, const QskBoxBorderColors& colors )
            : m_in( in )
            , m_out( out )
            , m_colors( colors )
            , m_isVisible( ( in != out ) && colors.isVisible() )
            , m_isMonochrome( !m_isVisible || colors.isMonochrome() )
        {
        }

        int lineCount() const
        {
            if ( !m_isVisible )
                return 0;

            // We can build a rectangular border from the 4 diagonal
            // lines at the corners, but need an additional line
            // for closing the border.

            int n = 4 + 1;

            if ( !m_isMonochrome )
            {
                const int gradientLines = -1
                    + m_colors.left().stepCount()
                    + m_colors.top().stepCount()
                    + m_colors.right().stepCount()
                    + m_colors.bottom().stepCount();

                n += qMax( gradientLines, 0 );
            }

            return n;
        }

        ColoredLine* addLines( ColoredLine* lines ) const
        {
            if ( !m_isVisible )
                return lines;

            const QLineF cl[4] =
            {
                { m_in.right, m_in.bottom, m_out.right, m_out.bottom },
                { m_in.left, m_in.bottom, m_out.left, m_out.bottom },
                { m_in.left, m_in.top, m_out.left, m_out.top },
                { m_in.right, m_in.top, m_out.right, m_out.top }
            };

            if ( m_isMonochrome )
            {
                const Color c = m_colors.left().rgbStart();

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

                lines = addGradientLines( cl[0], cl[1], m_colors.bottom(), lines );
                lines = addGradientLines( cl[1], cl[2], m_colors.left(), lines );
                lines = addGradientLines( cl[2], cl[3], m_colors.top(), lines );
                lines = addGradientLines( cl[3], cl[0], m_colors.right(), lines );
            }

            return lines;
        }

      private:
        const Quad m_in, m_out;
        const QskBoxBorderColors& m_colors;
        const bool m_isVisible;
        const bool m_isMonochrome;
    };

    class HVRectIterator
    {
      public:
        inline HVRectIterator( const Quad& rect, const QLineF& vector )
            : m_rect( rect )
            , m_vertical( vector.x1() == vector.x2() )
        {
            if ( m_vertical )
            {
                m_t = vector.y1();
                m_dt = vector.dy();

                m_values[0] = ( rect.top - m_t ) / m_dt;
                m_values[1] = ( rect.bottom - m_t ) / m_dt;
            }
            else
            {
                m_t = vector.x1();
                m_dt = vector.dx();

                m_values[0] = ( rect.left - m_t ) / m_dt;
                m_values[1] = ( rect.right - m_t ) / m_dt;
            }
        }

        inline void setGradientLine( qreal value, Color color, ColoredLine* line )
        {
            const auto v = m_t + value * m_dt;

            if ( m_vertical )
                line->setHLine( m_rect.left, m_rect.right, v, color );
            else
                line->setVLine( v, m_rect.top, m_rect.bottom, color );
        }

        inline void setContourLine( Color color, ColoredLine* line )
        {
            if ( m_vertical )
            {
                const auto y = m_step ? m_rect.bottom : m_rect.top;
                line->setLine( m_rect.left, y, m_rect.right, y, color );
            }
            else
            {
                const auto x = m_step ? m_rect.right : m_rect.left;
                line->setLine( x, m_rect.top, x, m_rect.bottom, color );
            }
        }

        inline qreal valueBegin() const { return m_values[ 0 ]; }
        inline qreal valueEnd() const { return m_values[ 1 ]; }
        inline qreal value() const { return m_values[ m_step ? 1 : 0 ]; }

        inline bool advance() { return m_step++ == 0; }

      private:
        const Quad& m_rect;
        qreal m_t, m_dt;
        qreal m_values[2];
        const bool m_vertical;
        int m_step = 0;
    };

    class DRectIterator
    {
      public:
        inline DRectIterator( const Quad& quad, const QLineF& vector )
        {
            m_v.x = vector.x1();
            m_v.y = vector.y1();
            m_v.dx = vector.dx();
            m_v.dy = vector.dy();

            /*
                We calculate the values at the corners and order them
                in increasing order
             */

            const qreal lx = ( quad.left - m_v.x ) * m_v.dx;
            const qreal rx = ( quad.right - m_v.x ) * m_v.dx;
            const qreal ty = ( quad.top - m_v.y ) * m_v.dy;
            const qreal by = ( quad.bottom - m_v.y ) * m_v.dy;

            const qreal dot = m_v.dx * m_v.dx + m_v.dy * m_v.dy;

            const qreal tl = ( lx + ty ) / dot;
            const qreal tr = ( rx + ty ) / dot;
            const qreal bl = ( lx + by ) / dot;
            const qreal br = ( rx + by ) / dot;

            if ( ( m_v.dy >= 0.0 ) == ( m_v.dx >= 0.0 ) )
            {
                m_corners[0] = { { quad.left, quad.top }, tl };
                m_corners[1] = { { quad.right, quad.top }, tr };
                m_corners[2] = { { quad.left, quad.bottom }, bl };
                m_corners[3] = { { quad.right, quad.bottom }, br };
            }
            else
            {
                m_corners[0] = { { quad.left, quad.bottom }, bl };
                m_corners[1] = { { quad.right, quad.bottom }, br };
                m_corners[2] = { { quad.left, quad.top }, tl };
                m_corners[3] = { { quad.right, quad.top }, tr };
            }

            if ( m_corners[0].value > m_corners[3].value )
                qSwap( m_corners[0], m_corners[3] );

            if ( m_corners[1].value > m_corners[2].value )
                qSwap( m_corners[1], m_corners[2] );
        }

        inline void setGradientLine( qreal value, Color color, ColoredLine* line )
        {
            const qreal m = m_v.dy / m_v.dx;

            const qreal x = m_v.x + m_v.dx * value;
            const qreal y = m_v.y + m_v.dy * value;

            const bool on = m_corners[0].pos.x() == m_corners[1].pos.x();

            QPointF p1, p2;

            switch( m_step )
            {
                case 1:
                {
                    p1 = p2 = m_corners[0].pos;

                    if ( on )
                    {
                        p1.ry() = y + ( x - p1.x() ) / m;
                        p2.rx() = x + ( y - p2.y() ) * m;
                    }
                    else
                    {
                        p1.rx() = x + ( y - p1.y() ) * m;
                        p2.ry() = y + ( x - p2.x() ) / m;
                    }

                    break;
                }
                case 2:
                {
                    p1 = m_corners[1].pos;
                    p2 = m_corners[0].pos;

                    if ( on )
                    {
                        p1.rx() = x + ( y - p1.y() ) * m;
                        p2.rx() = x + ( y - p2.y() ) * m;
                    }
                    else
                    {
                        p1.ry() = y + ( x - p1.x() ) / m;
                        p2.ry() = y + ( x - p2.x() ) / m;
                    }

                    break;
                }
                case 3:
                {
                    p1 = m_corners[1].pos;
                    p2 = m_corners[2].pos;

                    if ( on )
                    {
                        p1.rx() = x + ( y - p1.y() ) * m;
                        p2.ry() = y + ( x - p2.x() ) / m;
                    }
                    else
                    {
                        p1.ry() = y + ( x - p1.x() ) / m;
                        p2.rx() = x + ( y - p2.y() ) * m;
                    }
                    break;
                }
            }

            if ( p1.x() < p2.x() )
                line->setLine( p1.x(), p1.y(), p2.x(), p2.y(), color );
            else
                line->setLine( p2.x(), p2.y(), p1.x(), p1.y(), color );
        }

        inline void setContourLine( Color color, ColoredLine* line )
        {
            if( m_step == 0 || m_step == 3 )
            {
                const auto& p = m_corners[ m_step ].pos;
                line->setLine( p.x(), p.y(), p.x(), p.y(), color );
            }
            else
            {
                const qreal m = m_v.dy / m_v.dx;

                auto p1 = m_corners[ m_step - 1 ].pos;
                const auto& p2 = m_corners[ m_step ].pos;

                if ( p1.x() == m_corners[ m_step + 1 ].pos.x() )
                    p1.ry() = p2.y() + ( p2.x() - p1.x() ) / m;
                else
                    p1.rx() = p2.x() + ( p2.y() - p1.y() ) * m;

                if ( p1.x() <= p2.x() )
                    line->setLine( p1.x(), p1.y(), p2.x(), p2.y(), color );
                else
                    line->setLine( p2.x(), p2.y(), p1.x(), p1.y(), color );
            }
        }

        inline qreal valueBegin() const { return m_corners[ 0 ].value; }
        inline qreal valueEnd() const { return m_corners[ 3 ].value; }
        inline qreal value() const { return m_corners[ m_step ].value; }

        inline bool advance() { return ++m_step <= 3; }

      private:
        struct { qreal x, y, dx, dy; } m_v;
        struct { QPointF pos; qreal value; } m_corners[4];

        int m_step = 0;
    };
}

static ColoredLine* qskAddFillLines( const Quad& rect,
    const QskGradient& gradient, int lineCount, ColoredLine* line )
{
    const auto dir = gradient.linearDirection();

    if ( dir.isTilted() )
    {
        DRectIterator it( rect, dir.vector() );
        line = QskVertex::fillBox( it, gradient, lineCount, line );
    }
    else
    {
        HVRectIterator it( rect, dir.vector() );
        line = QskVertex::fillBox( it, gradient, lineCount, line );
    }

    return line;
}

void QskRectRenderer::renderBorderGeometry( const QRectF& rect,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    const Quad out = rect;
    const Quad in = qskValidOrEmptyInnerRect( rect, border.widths() );

    const bool noBorder = rect.isEmpty() || out == in;

    if ( const auto lines = allocateLines< Line >( geometry, noBorder ? 0 : 5 ) )
    {
        lines[0].setLine( in.right, in.bottom, out.right, out.bottom );
        lines[1].setLine( in.left, in.bottom, out.left, out.bottom );
        lines[2].setLine( in.left, in.top, out.left, out.top );
        lines[3].setLine( in.right, in.top, out.right, out.top );
        lines[4] = lines[ 0 ];
    }
}

void QskRectRenderer::renderFill0( const QskVertex::Quad& rect,
    const QskGradient& gradient, int lineCount, QskVertex::ColoredLine* line )
{
    qskAddFillLines( rect, gradient, lineCount, line );
}

void QskRectRenderer::renderFillGeometry( const QRectF& rect,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    const Quad in = qskValidOrEmptyInnerRect( rect, border.widths() );

    if ( const auto lines = allocateLines< Line >( geometry, in.isEmpty() ? 0 : 2 ) )
    {
        lines[0].setLine( in.left, in.top, in.right, in.top );
        lines[1].setLine( in.left, in.bottom, in.right, in.bottom );
    }
}

void QskRectRenderer::renderRect( const QRectF& rect,
    const QskBoxBorderMetrics& border, const QskBoxBorderColors& borderColors,
    const QskGradient& gradient, QSGGeometry& geometry )
{
    const Quad in = qskValidOrEmptyInnerRect( rect, border.widths() );
    const Quad out = rect;

    int fillLineCount = 0;

    if ( !in.isEmpty() && gradient.isVisible() )
    {
        const auto dir = gradient.linearDirection();

        fillLineCount = 2; // contour lines

        if ( !gradient.isMonochrome() )
        {
            if ( dir.isTilted() )
                fillLineCount += 2; // contour lines for the opposite corners

            fillLineCount += gradient.stepCount() - 1;

            if ( !gradient.linearDirection().contains( in ) )
            {
                /*
                    The gradient starts and/or ends inside of the rectangle
                    and we have to add extra gradient lines. As this is a
                    corner case we always allocate memory for both to avoid
                    making this calculation even more confusing.
                 */

                fillLineCount += 2;
            }
        }
    }

    const BorderStroker stroker( in, out, borderColors );
    const int borderLineCount = stroker.lineCount();

    const auto lines = allocateLines< ColoredLine >(
        geometry, borderLineCount + fillLineCount );

    if ( fillLineCount > 0 )
    {
        if ( fillLineCount == 2 )
        {
            const QskVertex::ColorMap fillMap( gradient );

            fillMap.setLine( in.left, in.top, in.right, in.top, lines + 0 );
            fillMap.setLine( in.left, in.bottom, in.right, in.bottom, lines + 1 );
        }
        else
        {
            qskAddFillLines( in, gradient, fillLineCount, lines );
        }
    }

    if ( borderLineCount > 0 )
        stroker.addLines( lines + fillLineCount );
}
