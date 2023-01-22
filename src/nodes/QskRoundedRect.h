/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#ifndef QSK_ROUNDED_RECT_H
#define QSK_ROUNDED_RECT_H

#include "QskVertex.h"
#include "QskRoundedRectRenderer.h"

class QskBoxShapeMetrics;
class QskBoxBorderMetrics;
class QskBoxBorderColors;

namespace QskRoundedRect
{
    enum
    {
        TopLeft = Qt::TopLeftCorner,
        TopRight = Qt::TopRightCorner,
        BottomLeft = Qt::BottomLeftCorner,
        BottomRight = Qt::BottomRightCorner
    };

    class Metrics
    {
      public:
        Metrics( const QRectF&, const QskBoxShapeMetrics&, const QskBoxBorderMetrics& );

        QskVertex::Quad outerQuad;
        QskVertex::Quad innerQuad;
        QskVertex::Quad centerQuad;

        int cornerStepCount() const
        {
            return corners[0].stepCount + corners[1].stepCount
                + corners[2].stepCount + corners[3].stepCount;
        }

        struct Corner
        {
            inline qreal xInner( qreal cos ) const
            {
                return centerX + sx * ( x0 + cos * rx );
            }

            inline qreal yInner( qreal sin ) const
            {
                return centerY + sy * ( y0 + sin * ry );
            }

            inline qreal xOuter( qreal cos ) const
            {
                return centerX + sx * ( cos * radiusX );
            }

            inline qreal yOuter( qreal sin ) const
            {
                return centerY + sy * ( sin * radiusY );
            }

            bool isCropped;

            qreal centerX, centerY;
            qreal radiusX, radiusY;
            qreal radiusInnerX, radiusInnerY;

            qreal x0, rx;
            qreal y0, ry;

            qreal sx, sy;

            int stepCount;

        } corners[ 4 ];

        bool isBorderRegular;
        bool isRadiusRegular;
        bool isTotallyCropped;

        Qt::Orientations stepSizeSymmetries;
        Qt::Orientation preferredOrientation;
    };

    class Stroker
    {
      public:
        inline Stroker( const Metrics& metrics )
            : m_metrics( metrics )
        {
        }

        /*
            QskVertex::Line ( = QSGGeometry::Point2D )

            Needed for:

                - monochrome coloring ( QSGSimpleMaterial )
                - clipping ( QSGClipNode )
                - shaders getting the color information from a color ramp
                  ( = QskGradientMatrial )
         */

        int fillLineCount() const;
        int borderLineCount() const;

        void createBorderLines( QskVertex::Line* ) const;
        void createFillLines( QskVertex::Line* ) const;

        /*
            QskVertex::ColoredLine ( = QSGGeometry::ColoredPoint2D )

            The color informtion is added to the geometry what allows
            using the same shader regardless of the colors, what ends
            up in better scene graph batching
         */

        int fillLineCount( const QskGradient& ) const;
        int borderLineCount( const QskBoxBorderColors& ) const;

        void createBox(
            QskVertex::ColoredLine*, const QskBoxBorderColors&,
            QskVertex::ColoredLine*, const QskGradient& ) const;

        void createFill( QskVertex::ColoredLine*, const QskGradient& ) const;
        void createBorder( QskVertex::ColoredLine*, const QskBoxBorderColors& ) const;

      private:
        void setBorderGradientLines( Qt::Edge,
            const QskBoxBorderColors&, QskVertex::ColoredLine* ) const;

        void createRegularBox(
            QskVertex::ColoredLine*, const QskBoxBorderColors&,
            QskVertex::ColoredLine*, const QskGradient& ) const;

        const Metrics& m_metrics;
    };
}

#endif
