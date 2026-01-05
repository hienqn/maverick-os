import Link from '@docusaurus/Link';
import Layout from '@theme/Layout';

import styles from './index.module.css';

export default function Home(): JSX.Element {
  return (
    <Layout
      title="Home"
      description="An educational operating system">
      <main className={styles.main}>
        <div className={styles.container}>
          <h1 className={styles.title}>MaverickOS</h1>
          <p className={styles.subtitle}>
            MaverickOS is an educational operating system extended from UC Berkeley's CS162 course.
            Beyond the original projects, I've added features like a write-ahead logging (WAL) system,
            virtual memory with demand paging, and extended POSIX system calls.
            This blog captures key OS concepts and maps them directly to concrete implementations,
            aiming to bridge the gap between lecture theory and real code.
          </p>
          <div className={styles.links}>
            <Link to="/blog" className={styles.link}>
              Blog
            </Link>
            <span className={styles.separator}>·</span>
            <Link to="https://github.com/hienqn/maverick-os" className={styles.link}>
              GitHub
            </Link>
          </div>
        </div>
      </main>
    </Layout>
  );
}
