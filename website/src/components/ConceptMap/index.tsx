import React, { useState } from 'react';
import Link from '@docusaurus/Link';
import styles from './styles.module.css';

export interface Concept {
  id: string;
  label: string;
  category: 'concept' | 'code' | 'file';
  description?: string;
  link?: string;
  filePath?: string;
  lineNumber?: number;
}

export interface ConceptRelation {
  from: string;
  to: string;
  label?: string;
}

interface ConceptMapProps {
  concepts: Concept[];
  relations: ConceptRelation[];
  title?: string;
}

// Simple layout algorithm
function layoutConcepts(
  concepts: Concept[],
  relations: ConceptRelation[]
): Map<string, { x: number; y: number }> {
  const positions = new Map<string, { x: number; y: number }>();

  // Group by category
  const conceptNodes = concepts.filter((c) => c.category === 'concept');
  const codeNodes = concepts.filter((c) => c.category !== 'concept');

  // Layout concepts on the left, code on the right
  conceptNodes.forEach((c, i) => {
    positions.set(c.id, {
      x: 100,
      y: 80 + i * 100,
    });
  });

  codeNodes.forEach((c, i) => {
    positions.set(c.id, {
      x: 500,
      y: 80 + i * 80,
    });
  });

  return positions;
}

export default function ConceptMap({
  concepts,
  relations,
  title,
}: ConceptMapProps): JSX.Element {
  const [selectedConcept, setSelectedConcept] = useState<string | null>(null);
  const [hoveredConcept, setHoveredConcept] = useState<string | null>(null);

  const positions = layoutConcepts(concepts, relations);
  const conceptMap = new Map(concepts.map((c) => [c.id, c]));

  // Find related concepts
  const getRelatedIds = (id: string): Set<string> => {
    const related = new Set<string>();
    relations.forEach((r) => {
      if (r.from === id) related.add(r.to);
      if (r.to === id) related.add(r.from);
    });
    return related;
  };

  const highlightedIds =
    selectedConcept || hoveredConcept
      ? getRelatedIds(selectedConcept || hoveredConcept!)
      : new Set<string>();

  const height = Math.max(
    ...Array.from(positions.values()).map((p) => p.y)
  ) + 120;

  return (
    <div className={styles.container}>
      {title && <h4 className={styles.title}>{title}</h4>}

      <div className={styles.legend}>
        <span className={styles.legendItem}>
          <span className={`${styles.legendDot} ${styles.concept}`} />
          OS Concept
        </span>
        <span className={styles.legendItem}>
          <span className={`${styles.legendDot} ${styles.code}`} />
          Code Location
        </span>
      </div>

      <svg viewBox={`0 0 700 ${height}`} className={styles.svg}>
        {/* Relations */}
        {relations.map((rel, idx) => {
          const from = positions.get(rel.from);
          const to = positions.get(rel.to);
          if (!from || !to) return null;

          const isHighlighted =
            highlightedIds.has(rel.from) || highlightedIds.has(rel.to);

          return (
            <g key={`rel-${idx}`}>
              <line
                x1={from.x + 80}
                y1={from.y + 25}
                x2={to.x}
                y2={to.y + 25}
                className={`${styles.relation} ${
                  isHighlighted ? styles.highlighted : ''
                }`}
              />
              {rel.label && (
                <text
                  x={(from.x + 80 + to.x) / 2}
                  y={(from.y + to.y) / 2 + 15}
                  className={styles.relationLabel}
                >
                  {rel.label}
                </text>
              )}
            </g>
          );
        })}

        {/* Nodes */}
        {concepts.map((concept) => {
          const pos = positions.get(concept.id);
          if (!pos) return null;

          const isSelected = selectedConcept === concept.id;
          const isHovered = hoveredConcept === concept.id;
          const isRelated = highlightedIds.has(concept.id);

          return (
            <g
              key={concept.id}
              className={styles.node}
              onClick={() =>
                setSelectedConcept(isSelected ? null : concept.id)
              }
              onMouseEnter={() => setHoveredConcept(concept.id)}
              onMouseLeave={() => setHoveredConcept(null)}
            >
              <rect
                x={pos.x}
                y={pos.y}
                width={concept.category === 'concept' ? 160 : 180}
                height={50}
                rx="8"
                className={`${styles.nodeRect} ${styles[concept.category]} ${
                  isSelected || isHovered || isRelated ? styles.active : ''
                }`}
              />
              <text
                x={pos.x + (concept.category === 'concept' ? 80 : 90)}
                y={pos.y + 25}
                textAnchor="middle"
                dominantBaseline="middle"
                className={styles.nodeLabel}
              >
                {concept.label}
              </text>
              {concept.filePath && (
                <text
                  x={pos.x + 90}
                  y={pos.y + 42}
                  textAnchor="middle"
                  className={styles.filePath}
                >
                  {concept.filePath}
                  {concept.lineNumber && `:${concept.lineNumber}`}
                </text>
              )}
            </g>
          );
        })}
      </svg>

      {/* Detail panel */}
      {selectedConcept && (
        <div className={styles.detailPanel}>
          {(() => {
            const concept = conceptMap.get(selectedConcept);
            if (!concept) return null;
            return (
              <>
                <h5 className={styles.detailTitle}>{concept.label}</h5>
                {concept.description && (
                  <p className={styles.detailDescription}>
                    {concept.description}
                  </p>
                )}
                {concept.link && (
                  <Link to={concept.link} className={styles.detailLink}>
                    View Documentation →
                  </Link>
                )}
                {concept.filePath && (
                  <code className={styles.detailCode}>
                    {concept.filePath}
                    {concept.lineNumber && `:${concept.lineNumber}`}
                  </code>
                )}
              </>
            );
          })()}
        </div>
      )}

      <p className={styles.hint}>
        Click on a node to see details and related connections
      </p>
    </div>
  );
}
