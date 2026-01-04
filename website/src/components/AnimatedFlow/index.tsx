import React, { useState, useEffect } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import styles from './styles.module.css';

export interface FlowState {
  id: string;
  label: string;
  description?: string;
  color?: string;
}

export interface FlowTransition {
  from: string;
  to: string;
  label?: string;
  duration?: number;
}

interface AnimatedFlowProps {
  states: FlowState[];
  transitions: FlowTransition[];
  title?: string;
  autoPlay?: boolean;
  loop?: boolean;
}

export default function AnimatedFlow({
  states,
  transitions,
  title,
  autoPlay = false,
  loop = true,
}: AnimatedFlowProps): JSX.Element {
  const [currentStep, setCurrentStep] = useState(0);
  const [isPlaying, setIsPlaying] = useState(autoPlay);
  const [activeStates, setActiveStates] = useState<Set<string>>(new Set());

  const currentTransition = transitions[currentStep];

  useEffect(() => {
    if (currentTransition) {
      setActiveStates(new Set([currentTransition.from, currentTransition.to]));
    }
  }, [currentStep, currentTransition]);

  useEffect(() => {
    if (!isPlaying) return;

    const timer = setInterval(() => {
      setCurrentStep((prev) => {
        const next = prev + 1;
        if (next >= transitions.length) {
          if (loop) return 0;
          setIsPlaying(false);
          return prev;
        }
        return next;
      });
    }, currentTransition?.duration || 1500);

    return () => clearInterval(timer);
  }, [isPlaying, currentStep, loop, transitions.length]);

  const handlePlay = () => setIsPlaying(true);
  const handlePause = () => setIsPlaying(false);
  const handleStep = (direction: 'prev' | 'next') => {
    setIsPlaying(false);
    setCurrentStep((prev) => {
      if (direction === 'next') {
        return Math.min(prev + 1, transitions.length - 1);
      }
      return Math.max(prev - 1, 0);
    });
  };
  const handleReset = () => {
    setIsPlaying(false);
    setCurrentStep(0);
  };

  return (
    <div className={styles.container}>
      {title && <h4 className={styles.title}>{title}</h4>}

      <div className={styles.flowDiagram}>
        <div className={styles.statesContainer}>
          {states.map((state, index) => (
            <motion.div
              key={state.id}
              className={`${styles.stateBox} ${
                activeStates.has(state.id) ? styles.active : ''
              }`}
              style={{
                backgroundColor: activeStates.has(state.id)
                  ? state.color || 'var(--ifm-color-primary)'
                  : undefined,
              }}
              initial={{ scale: 1 }}
              animate={{
                scale: activeStates.has(state.id) ? 1.05 : 1,
                boxShadow: activeStates.has(state.id)
                  ? '0 4px 20px rgba(0,0,0,0.2)'
                  : '0 2px 8px rgba(0,0,0,0.1)',
              }}
              transition={{ duration: 0.3 }}
            >
              <span className={styles.stateLabel}>{state.label}</span>
            </motion.div>
          ))}
        </div>

        <AnimatePresence mode="wait">
          {currentTransition && (
            <motion.div
              key={currentStep}
              className={styles.transitionLabel}
              initial={{ opacity: 0, y: 10 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -10 }}
              transition={{ duration: 0.3 }}
            >
              <div className={styles.arrow}>→</div>
              <span>{currentTransition.label}</span>
            </motion.div>
          )}
        </AnimatePresence>
      </div>

      <div className={styles.description}>
        {currentTransition && (
          <p>
            <strong>{states.find(s => s.id === currentTransition.from)?.label}</strong>
            {' → '}
            <strong>{states.find(s => s.id === currentTransition.to)?.label}</strong>
            {currentTransition.label && `: ${currentTransition.label}`}
          </p>
        )}
      </div>

      <div className={styles.controls}>
        <button onClick={handleReset} title="Reset">
          ⟲
        </button>
        <button onClick={() => handleStep('prev')} disabled={currentStep === 0}>
          ◀
        </button>
        {isPlaying ? (
          <button onClick={handlePause}>⏸</button>
        ) : (
          <button onClick={handlePlay}>▶</button>
        )}
        <button
          onClick={() => handleStep('next')}
          disabled={currentStep >= transitions.length - 1}
        >
          ▶
        </button>
        <span className={styles.stepIndicator}>
          Step {currentStep + 1} / {transitions.length}
        </span>
      </div>
    </div>
  );
}
