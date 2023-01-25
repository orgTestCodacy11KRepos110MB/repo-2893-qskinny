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

    ColoredLine* qskAddFillLines( const Quad& rect,
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

}

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

        void setFillLines( QskVertex::ColoredLine* lines ) const
        {
            if ( m_gradient.isMonochrome() )
            {
                const QskVertex::ColorMap fillMap( m_gradient );

                fillMap.setLine( m_in.left, m_in.top, m_in.right, m_in.top, lines + 0 );
                fillMap.setLine( m_in.left, m_in.bottom, m_in.right, m_in.bottom, lines + 1 );
            }
            else
            {
                // m_fillLineCount !
                qskAddFillLines( m_in, m_gradient, fillCount(), lines );
            }
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
        stroker.setFillLines( lines );

    if ( borderCount )
        stroker.setBorderLines( lines + fillCount );
}

void QskRectRenderer::renderFill0( const QskVertex::Quad& rect,
    const QskGradient& gradient, int lineCount, QskVertex::ColoredLine* line )
{
    qskAddFillLines( rect, gradient, lineCount, line );
}
