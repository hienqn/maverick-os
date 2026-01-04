import React, { useState } from 'react';
import CodeBlock from '@theme/CodeBlock';
import styles from './styles.module.css';

export interface WalkthroughStep {
  lines: [number, number]; // [start, end] line numbers (1-indexed)
  title: string;
  explanation: string;
}

interface CodeWalkthroughProps {
  code: string;
  language?: string;
  title?: string;
  steps: WalkthroughStep[];
  sourceFile?: string;
}

export default function CodeWalkthrough({
  code,
  language = 'c',
  title,
  steps,
  sourceFile,
}: CodeWalkthroughProps): JSX.Element {
  const [currentStep, setCurrentStep] = useState(0);

  const lines = code.split('\n');
  const step = steps[currentStep];

  // Create highlighted code with current step lines emphasized
  const highlightedCode = lines
    .map((line, idx) => {
      const lineNum = idx + 1;
      const isHighlighted =
        lineNum >= step.lines[0] && lineNum <= step.lines[1];
      return { line, isHighlighted, lineNum };
    });

  // Generate line highlighting string for CodeBlock
  const highlightLines = `{${step.lines[0]}-${step.lines[1]}}`;

  return (
    <div className={styles.container}>
      {title && (
        <div className={styles.header}>
          <h4 className={styles.title}>{title}</h4>
          {sourceFile && (
            <code className={styles.sourceFile}>{sourceFile}</code>
          )}
        </div>
      )}

      <div className={styles.walkthrough}>
        <div className={styles.codePanel}>
          <div className={styles.codeContainer}>
            {highlightedCode.map(({ line, isHighlighted, lineNum }) => (
              <div
                key={lineNum}
                className={`${styles.codeLine} ${
                  isHighlighted ? styles.highlighted : ''
                }`}
              >
                <span className={styles.lineNumber}>{lineNum}</span>
                <pre className={styles.lineContent}>
                  <code>{line || ' '}</code>
                </pre>
              </div>
            ))}
          </div>
        </div>

        <div className={styles.explanationPanel}>
          <div className={styles.stepNavigation}>
            {steps.map((s, idx) => (
              <button
                key={idx}
                className={`${styles.stepDot} ${
                  idx === currentStep ? styles.active : ''
                }`}
                onClick={() => setCurrentStep(idx)}
                title={s.title}
              />
            ))}
          </div>

          <div className={styles.stepContent}>
            <h5 className={styles.stepTitle}>
              Step {currentStep + 1}: {step.title}
            </h5>
            <p className={styles.stepExplanation}>{step.explanation}</p>
            <div className={styles.lineRange}>
              Lines {step.lines[0]}–{step.lines[1]}
            </div>
          </div>

          <div className={styles.navigationButtons}>
            <button
              onClick={() => setCurrentStep((p) => Math.max(0, p - 1))}
              disabled={currentStep === 0}
            >
              ← Previous
            </button>
            <button
              onClick={() =>
                setCurrentStep((p) => Math.min(steps.length - 1, p + 1))
              }
              disabled={currentStep === steps.length - 1}
            >
              Next →
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
