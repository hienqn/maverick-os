import React, { useEffect, useRef, useState } from 'react';
import styles from './styles.module.css';

export interface DiagramNode {
  id: string;
  label: string;
  x: number;
  y: number;
  width?: number;
  height?: number;
  color?: string;
  link?: string;
  description?: string;
}

export interface DiagramEdge {
  from: string;
  to: string;
  label?: string;
  dashed?: boolean;
}

interface InteractiveDiagramProps {
  nodes: DiagramNode[];
  edges: DiagramEdge[];
  title?: string;
  width?: number;
  height?: number;
  onNodeClick?: (node: DiagramNode) => void;
}

export default function InteractiveDiagram({
  nodes,
  edges,
  title,
  width = 800,
  height = 500,
  onNodeClick,
}: InteractiveDiagramProps): JSX.Element {
  const svgRef = useRef<SVGSVGElement>(null);
  const [hoveredNode, setHoveredNode] = useState<string | null>(null);
  const [tooltip, setTooltip] = useState<{
    x: number;
    y: number;
    text: string;
  } | null>(null);

  const nodeMap = new Map(nodes.map((n) => [n.id, n]));

  const getEdgePath = (edge: DiagramEdge): string => {
    const from = nodeMap.get(edge.from);
    const to = nodeMap.get(edge.to);
    if (!from || !to) return '';

    const fromW = from.width || 120;
    const fromH = from.height || 50;
    const toW = to.width || 120;
    const toH = to.height || 50;

    const fromCx = from.x + fromW / 2;
    const fromCy = from.y + fromH / 2;
    const toCx = to.x + toW / 2;
    const toCy = to.y + toH / 2;

    // Calculate edge points on box boundaries
    const dx = toCx - fromCx;
    const dy = toCy - fromCy;
    const angle = Math.atan2(dy, dx);

    const fromX = fromCx + (fromW / 2) * Math.cos(angle);
    const fromY = fromCy + (fromH / 2) * Math.sin(angle);
    const toX = toCx - (toW / 2) * Math.cos(angle);
    const toY = toCy - (toH / 2) * Math.sin(angle);

    return `M ${fromX} ${fromY} L ${toX} ${toY}`;
  };

  const handleNodeClick = (node: DiagramNode) => {
    if (onNodeClick) {
      onNodeClick(node);
    } else if (node.link) {
      window.location.href = node.link;
    }
  };

  const handleNodeHover = (node: DiagramNode | null, e?: React.MouseEvent) => {
    if (node && node.description && e) {
      setHoveredNode(node.id);
      setTooltip({
        x: e.clientX,
        y: e.clientY,
        text: node.description,
      });
    } else {
      setHoveredNode(null);
      setTooltip(null);
    }
  };

  return (
    <div className={styles.container}>
      {title && <h4 className={styles.title}>{title}</h4>}
      <div className={styles.diagramWrapper}>
        <svg
          ref={svgRef}
          viewBox={`0 0 ${width} ${height}`}
          className={styles.diagram}
        >
          <defs>
            <marker
              id="arrowhead"
              markerWidth="10"
              markerHeight="7"
              refX="9"
              refY="3.5"
              orient="auto"
            >
              <polygon
                points="0 0, 10 3.5, 0 7"
                fill="var(--ifm-color-gray-500)"
              />
            </marker>
          </defs>

          {/* Edges */}
          {edges.map((edge, idx) => (
            <g key={`edge-${idx}`}>
              <path
                d={getEdgePath(edge)}
                fill="none"
                stroke="var(--ifm-color-gray-400)"
                strokeWidth="2"
                strokeDasharray={edge.dashed ? '5,5' : undefined}
                markerEnd="url(#arrowhead)"
              />
              {edge.label && (
                <text
                  x={
                    ((nodeMap.get(edge.from)?.x || 0) +
                      (nodeMap.get(edge.to)?.x || 0)) /
                      2 +
                    60
                  }
                  y={
                    ((nodeMap.get(edge.from)?.y || 0) +
                      (nodeMap.get(edge.to)?.y || 0)) /
                      2 +
                    25
                  }
                  className={styles.edgeLabel}
                >
                  {edge.label}
                </text>
              )}
            </g>
          ))}

          {/* Nodes */}
          {nodes.map((node) => {
            const w = node.width || 120;
            const h = node.height || 50;
            const isHovered = hoveredNode === node.id;

            return (
              <g
                key={node.id}
                className={`${styles.node} ${isHovered ? styles.hovered : ''}`}
                onClick={() => handleNodeClick(node)}
                onMouseEnter={(e) => handleNodeHover(node, e)}
                onMouseLeave={() => handleNodeHover(null)}
                style={{ cursor: node.link ? 'pointer' : 'default' }}
              >
                <rect
                  x={node.x}
                  y={node.y}
                  width={w}
                  height={h}
                  rx="8"
                  fill={node.color || 'var(--ifm-color-primary)'}
                  className={styles.nodeRect}
                />
                <text
                  x={node.x + w / 2}
                  y={node.y + h / 2}
                  textAnchor="middle"
                  dominantBaseline="middle"
                  className={styles.nodeLabel}
                >
                  {node.label}
                </text>
              </g>
            );
          })}
        </svg>

        {tooltip && (
          <div
            className={styles.tooltip}
            style={{
              left: tooltip.x + 10,
              top: tooltip.y + 10,
            }}
          >
            {tooltip.text}
          </div>
        )}
      </div>
      <p className={styles.hint}>Click on nodes to navigate to documentation</p>
    </div>
  );
}
