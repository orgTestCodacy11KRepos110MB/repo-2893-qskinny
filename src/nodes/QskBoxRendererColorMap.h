/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#ifndef QSK_BOX_RENDERER_COLOR_MAP_H
#define QSK_BOX_RENDERER_COLOR_MAP_H

#include <QskGradient.h>
#include <QskGradientDirection.h>
#include <QskVertex.h>

#include <cassert>

class QskBoxShapeMetrics;

namespace QskVertex
{
    inline ColoredLine* addGradientLines( const QLineF& l1, const QLineF& l2,
        const QskGradient& gradient, ColoredLine* lines )
    {
        const auto stops = gradient.stops();

        if ( stops.first().position() > 0.0 )
            ( lines++ )->setLine( l1, stops.first().rgb() );

        for( const auto& stop : stops )
        {
            const auto p1 = l1.p1() + stop.position() * ( l2.p1() - l1.p1() );
            const auto p2 = l1.p2() + stop.position() * ( l2.p2() - l1.p2() );

            ( lines++ )->setLine( p1, p2, stop.rgb() );
        }

        if ( stops.last().position() < 1.0 )
            ( lines++ )->setLine( l2, stops.last().rgb() );

        return lines;
    }

    class ColorMap
    {
      public:
        inline ColorMap( const QskGradient& gradient )
            : m_isMonochrome( gradient.isMonochrome() )
            , m_color1( gradient.rgbStart() )
            , m_color2( gradient.rgbEnd() )
        {
            if ( !m_isMonochrome )
            {
                const auto dir = gradient.linearDirection();

                m_x = dir.x1();
                m_y = dir.y1();
                m_dx = dir.x2() - dir.x1();
                m_dy = dir.y2() - dir.y1();
                m_dot = m_dx * m_dx + m_dy * m_dy;
            }
        }

        inline void setLine( qreal x1, qreal y1, qreal x2, qreal y2,
            QskVertex::ColoredLine* line ) const
        {
            if ( m_isMonochrome )
            {
                line->setLine( x1, y1, x2, y2, m_color1 );
            }
            else
            {
                const auto c1 = colorAt( x1, y1 );
                const auto c2 = colorAt( x2, y2 );

                line->setLine( x1, y1, c1, x2, y2, c2 );
            }
        }

      private:
        inline QskVertex::Color colorAt( qreal x, qreal y ) const
        {
            return m_color1.interpolatedTo( m_color2, valueAt( x, y ) );
        }

        inline qreal valueAt( qreal x, qreal y ) const
        {
            const qreal dx = x - m_x;
            const qreal dy = y - m_y;

            return ( dx * m_dx + dy * m_dy ) / m_dot;
        }

        const bool m_isMonochrome;

        qreal m_x, m_y, m_dx, m_dy, m_dot;

        const QskVertex::Color m_color1;
        const QskVertex::Color m_color2;
    };

    class GradientIterator
    {
      public:
        inline GradientIterator( const Color color )
            : m_color1( color )
            , m_color2( color )
        {
        }

        inline GradientIterator( const Color& color1, const Color& color2 )
            : m_color1( color1 )
            , m_color2( color2 )
        {
        }

        inline GradientIterator( const QskGradientStops& stops )
            : m_stops( stops )
            , m_color1( stops.first().rgb() )
            , m_color2( m_color1 )
            , m_pos1( stops.first().position() )
            , m_pos2( m_pos1 )
            , m_index( 0 )
        {
        }

        inline qreal position() const
        {
            return m_pos2;
        }

        inline Color color() const
        {
            return m_color2;
        }

        inline Color colorAt( qreal pos ) const
        {
            if ( m_color1 == m_color2 )
                return m_color1;

            if ( m_index < 0 )
            {
                return m_color1.interpolatedTo( m_color2, pos );
            }
            else
            {
                if ( m_pos2 == m_pos1 )
                    return m_color1;

                const auto r = ( pos - m_pos1 ) / ( m_pos2 - m_pos1 );
                return m_color1.interpolatedTo( m_color2, r );
            }
        }

        inline bool advance()
        {
            if ( m_index < 0 )
                return true;

            m_pos1 = m_pos2;
            m_color1 = m_color2;

            if ( ++m_index < m_stops.size() )
            {
                const auto& s = m_stops[ m_index ];

                m_pos2 = s.position();
                m_color2 = s.rgb();
            }

            return !isDone();
        }

        inline bool isDone() const
        {
            if ( m_index < 0 )
                return true;

            return m_index >= m_stops.size();
        }

      private:
        const QskGradientStops m_stops;

        Color m_color1, m_color2;

        qreal m_pos1 = 0.0;
        qreal m_pos2 = 1.0;

        int m_index = -1;
    };

    inline ColoredLine* fillUp( ColoredLine* lines, const ColoredLine& l, int count )
    {
        for ( int i = 0; i < count; i++ )
            *lines++ = l;

        return lines;
    }

    template< class ContourIterator, class GradientIterator >
    ColoredLine* fillOrdered( ContourIterator& contourIt,
        GradientIterator& gradientIt, int lineCount, ColoredLine* lines )
    {
        /*
             When the the vector exceeds [ 0.0, 1.0 ] we might have
             gradient lines lying outside the contour.
             This effect could be precalculated - however we might end
             up difficult code with potential bugs.

             So we allow the allocation code to ignore the effect by
             adding duplicates of the last line.
         */

        const auto pos1 = contourIt.valueBegin();
        const auto pos2 = contourIt.valueEnd();

        ColoredLine* l = lines;

        do
        {
            while ( !gradientIt.isDone() && ( gradientIt.position() < contourIt.value() ) )
            {
                const auto pos = gradientIt.position();

                /*
                    When having a gradient vector beyond [0,1]
                    we will have gradient lines outside of the contour
                 */

                if ( pos > pos1 && pos < pos2 )
                    contourIt.setGradientLine( pos, gradientIt.color(), l++ );

                gradientIt.advance();
            }

            const auto color = gradientIt.colorAt( contourIt.value() );
            contourIt.setContourLine( color, l++ );

        } while ( contourIt.advance() );

        if ( lineCount >= 0 )
        {
            const auto count = lineCount - ( l - lines );
            Q_ASSERT( count >= 0 );

            /*
                Precalculating all situations where gradient and contour lines
                are matching and doing an precise allocation makes the code
                error prone and hard to read. So we allow a defensive allocation
                strategy and simply fill up the memory with duplicates of the
                final lines.
             */
            if ( count > 0 )
                l = QskVertex::fillUp( l, *( l - 1 ), count );
        }

        return l;
    }

    template< class ContourIterator >
    ColoredLine* fillBox( ContourIterator& contourIt,
        const QskGradient& gradient, int lineCount, ColoredLine* lines )
    {
        if ( gradient.stepCount() == 1 )
        {
            /*
                when the gradient vector does not cover the complete contour
                we need to insert gradient lines
             */
            if ( contourIt.valueBegin() >= 0.0 && contourIt.valueEnd() <= 1.0 )
            {
                GradientIterator gradientIt( gradient.rgbStart(), gradient.rgbEnd() );
                return fillOrdered( contourIt, gradientIt, lineCount, lines );
            }
        }

        GradientIterator gradientIt( gradient.stops() );
        return fillOrdered( contourIt, gradientIt, lineCount, lines );
    }
}

#endif
